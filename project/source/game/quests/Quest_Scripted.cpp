#include "Pch.h"
#include "GameCore.h"
#include "Quest_Scripted.h"
#include "QuestScheme.h"
#include "ScriptManager.h"
#include "World.h"
#include "Game.h"
#include "Journal.h"
#include "GlobalGui.h"
#include "GameMessages.h"
#include "QuestManager.h"
#include "GameFile.h"
#include "City.h"
#include "Encounter.h"
#include <angelscript.h>
#pragma warning(error: 4062)

Quest_Scripted::~Quest_Scripted()
{
	if(instance)
		instance->Release();
}

void Quest_Scripted::Start()
{
	start_loc = W.GetCurrentLocationIndex();
	quest_id = Q_SCRIPTED;
	type = scheme->type;

	CreateInstance();

	// call Startup
	if(!scheme->f_startup)
		return;
	BeforeCall();
	SM.RunScript(scheme->f_startup, instance);
	AfterCall();
}

void Quest_Scripted::CreateInstance()
{
	asIScriptFunction* factory = scheme->script_type->GetFactoryByIndex(0);
	SM.RunScript(factory, nullptr, [this](asIScriptContext* ctx, int stage)
	{
		if(stage == 1)
		{
			void* ptr = ctx->GetAddressOfReturnValue();
			instance = *(asIScriptObject**)ptr;
			instance->AddRef();
		}
	});
}

void Quest_Scripted::Save(GameWriter& f)
{
	Quest::Save(f);

	f << scheme->id;
	f << timeout_days;

	uint props = scheme->script_type->GetPropertyCount();
	for(uint i = 0; i < props; ++i)
	{
		int type_id;
		scheme->script_type->GetProperty(i, nullptr, &type_id);
		Var::Type var_type = SM.GetVarType(type_id);
		void* ptr = instance->GetAddressOfProperty(i);
		switch(var_type)
		{
		case Var::Type::None:
			break;
		case Var::Type::Bool:
			f << *(bool*)ptr;
			break;
		case Var::Type::Int:
			f << *(int*)ptr;
			break;
		case Var::Type::Float:
			f << *(float*)ptr;
			break;
		case Var::Type::Item:
			{
				Item* item = *(Item**)ptr;
				if(item)
					f << item->id;
				else
					f.Write0();
			}
			break;
		case Var::Type::Location:
			{
				Location* loc = *(Location**)ptr;
				if(loc)
					f << loc->index;
				else
					f << -1;
			}
			break;
		case Var::Type::Encounter:
			{
				Encounter* enc = *(Encounter**)ptr;
				if(enc)
					f << enc->index;
				else
					f << -1;
			}
			break;
		}
	}
}

bool Quest_Scripted::Load(GameReader& f)
{
	Quest::Load(f);

	const string& scheme_id = f.ReadString1();
	scheme = QuestScheme::TryGet(scheme_id);
	if(!scheme)
		throw Format("Missing quest scheme '%s'.", scheme_id.c_str());
	f >> timeout_days;

	CreateInstance();

	// properties
	uint props = scheme->script_type->GetPropertyCount();
	for(uint i = 0; i < props; ++i)
	{
		int type_id;
		scheme->script_type->GetProperty(i, nullptr, &type_id);
		Var::Type var_type = SM.GetVarType(type_id);
		void* ptr = instance->GetAddressOfProperty(i);
		switch(var_type)
		{
		case Var::Type::None:
			break;
		case Var::Type::Bool:
			*(bool*)ptr = f.Read<bool>();
			break;
		case Var::Type::Int:
			*(int*)ptr = f.Read<int>();
			break;
		case Var::Type::Float:
			*(float*)ptr = f.Read<float>();
			break;
		case Var::Type::Item:
			{
				const string& item_id = f.ReadString1();
				if(!item_id.empty())
					*(Item**)ptr = Item::Get(item_id);
				else
					*(Item**)ptr = nullptr;
			}
			break;
		case Var::Type::Location:
			{
				int index = f.Read<int>();
				if(index != -1)
					*(Location**)ptr = W.GetLocation(index);
				else
					*(Location**)ptr = nullptr;
			}
			break;
		case Var::Type::Encounter:
			{
				int index = f.Read<int>();
				if(index != -1)
					*(Encounter**)ptr = W.GetEncounter(index);
				else
					*(Encounter**)ptr = nullptr;
			}
			break;
		}
	}

	return true;
}

GameDialog* Quest_Scripted::GetDialog(int type2)
{
	if(type2 == QUEST_DIALOG_START)
		return scheme->GetDialog("start");
	else
	{
		assert(0); // not implemented
		return nullptr;
	}
}

GameDialog* Quest_Scripted::GetDialog(const string& dialog_id)
{
	GameDialog* dialog = scheme->GetDialog(dialog_id);
	if(!dialog)
		throw new ScriptException("Missing quest dialog '%s'.", dialog_id.c_str());
	return dialog;
}

void Quest_Scripted::BeforeCall()
{
	if(call_depth == 0)
	{
		journal_state = JournalState::None;
		journal_changes = 0;
		SM.GetContext().quest = this;
	}
	++call_depth;
}

void Quest_Scripted::AfterCall()
{
	--call_depth;
	if(call_depth != 0)
		return;
	if(journal_changes || journal_state == JournalState::Changed)
	{
		game->gui->journal->NeedUpdate(Journal::Quests, quest_index);
		game->gui->messages->AddGameMsg3(GMS_JOURNAL_UPDATED);
		if(Net::IsOnline())
		{
			NetChange& c = Add1(Net::changes);
			c.id = refid;
			c.type = NetChange::UPDATE_QUEST;
			c.count = journal_changes;
		}
	}
	SM.GetContext().quest = nullptr;
}

void Quest_Scripted::SetProgress(int prog2)
{
	if(prog == prog2)
		return;
	prog = prog2;
	BeforeCall();
	SM.RunScript(scheme->f_progress, instance);
	AfterCall();
}

void Quest_Scripted::AddEntry(const string& str)
{
	if(journal_state != JournalState::Added)
		++journal_changes;
	msgs.push_back(str);
}

void Quest_Scripted::SetStarted(const string& name)
{
	assert(state == Quest::Hidden);
	assert(journal_state == JournalState::None);
	OnStart(name.c_str());
	journal_state = JournalState::Added;
}

void Quest_Scripted::SetCompleted()
{
	assert(journal_state == JournalState::None);
	journal_state = JournalState::Changed;
	state = Quest::Completed;
	if(type == QuestType::Mayor)
		((City&)GetStartLocation()).quest_mayor = CityQuestState::None;
	else if(type == QuestType::Captain)
		((City&)GetStartLocation()).quest_captain = CityQuestState::None;
	Cleanup();
}

void Quest_Scripted::SetFailed()
{
	assert(journal_state == JournalState::None);
	journal_state = JournalState::Changed;
	state = Quest::Failed;
	if(type == QuestType::Mayor)
		((City&)GetStartLocation()).quest_mayor = CityQuestState::Failed;
	else if(type == QuestType::Captain)
		((City&)GetStartLocation()).quest_captain = CityQuestState::Failed;
	Cleanup();
}

void Quest_Scripted::SetTimeout(int days)
{
	assert(Any(state, Quest::Hidden, Quest::Started));
	assert(timeout_days == -1);
	assert(days > 0);
	timeout_days = days;
	QM.quests_timeout2.push_back(this);
}

bool Quest_Scripted::IsTimedout() const
{
	if(timeout_days == -1)
		return false;
	return W.GetWorldtime() - start_time > timeout_days;
}

bool Quest_Scripted::OnTimeout(TimeoutType ttype)
{
	ScriptEvent event(EVENT_TIMEOUT);
	timeout_days = -1;
	FireEvent(event);
	return true;
}

void Quest_Scripted::FireEvent(ScriptEvent& event)
{
	if(!scheme->f_event)
		return;
	BeforeCall();
	SM.RunScript(scheme->f_event, instance, [&event](asIScriptContext* ctx, int stage)
	{
		if(stage == 0)
			CHECKED(ctx->SetArgObject(0, &event));
	});
	AfterCall();
}

void Quest_Scripted::Cleanup()
{
	if(timeout_days != -1)
	{
		RemoveElementTry(QM.quests_timeout2, (Quest*)this);
		timeout_days = -1;
	}

	for(EventPtr& e : events)
	{
		switch(e.source)
		{
		case EventPtr::LOCATION:
			e.location->RemoveEventHandler(this, true);
			break;
		}
	}
	events.clear();

	for(Unit* unit : unit_dialogs)
		unit->RemoveDialog(this, true);
	unit_dialogs.clear();

	W.RemoveEncounter(this);
}

// Remove event ptr, must be called when removing real event
void Quest_Scripted::RemoveEventPtr(const EventPtr& event)
{
	LoopAndRemove(events, [&](EventPtr& e)
	{
		return event == e;
	});
}

void Quest_Scripted::RemoveDialogPtr(Unit* unit)
{
	LoopAndRemove(unit_dialogs, [=](Unit* u)
	{
		return unit == u;
	});
}

cstring Quest_Scripted::FormatString(const string& str)
{
	if(str == "date")
		return W.GetDate();
	else
	{
		assert(0);
		return Format("!!!%s!!!", str.c_str());
	}
}

string Quest_Scripted::GetString(int index)
{
	assert(SM.GetContext().quest == this);
	GameDialog* dialog = scheme->dialogs[0];
	if(index < 0 || index >= (int)dialog->texts.size())
		throw ScriptException("Invalid text index.");
	GameDialog::Text& text = dialog->texts[index];
	const string& str = dialog->strs[text.index];

	if(!text.formatted)
		return str;

	static string str_part;
	static string dialog_s_text;
	dialog_s_text.clear();

	for(uint i = 0, len = str.length(); i < len; ++i)
	{
		if(str[i] == '$')
		{
			str_part.clear();
			++i;
			if(str[i] == '(')
			{
				uint pos = FindClosingPos(str, i);
				int index = atoi(str.substr(i + 1, pos - i - 1).c_str());
				SM.RunScript(scheme->scripts.Get(DialogScripts::F_FORMAT), instance, [&](asIScriptContext* ctx, int stage)
				{
					if(stage == 0)
					{
						CHECKED(ctx->SetArgDWord(0, index));
					}
					else if(stage == 1)
					{
						string* result = (string*)ctx->GetAddressOfReturnValue();
						dialog_s_text += *result;
					}
				});
				i = pos;
			}
			else
			{
				while(str[i] != '$')
				{
					str_part.push_back(str[i]);
					++i;
				}
				dialog_s_text += FormatString(str_part);
			}
		}
		else
			dialog_s_text.push_back(str[i]);
	}

	return dialog_s_text.c_str();
}