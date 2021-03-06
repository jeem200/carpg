#include "Pch.h"
#include "LevelAreaContext.h"

#include "City.h"
#include "Game.h"
#include "Level.h"
#include "MultiInsideLocation.h"
#include "Net.h"
#include "SingleInsideLocation.h"
#include "World.h"

static ObjectPool<LevelAreaContext> LevelAreaContextPool;

//=================================================================================================
// Get area levels for selected location and level (in multilevel dungeon not generated levels are ignored for -1)
// Level have special meaning here
// >= 0 (dungeon_level, building index)
// -1 whole location
// -2 outside part of city/village
ForLocation::ForLocation(int loc, int level)
{
	Setup(world->GetLocation(loc), level);
}

//=================================================================================================
ForLocation::ForLocation(Location* loc, int level)
{
	Setup(loc, level);
}

//=================================================================================================
void ForLocation::Setup(Location* l, int level)
{
	ctx = LevelAreaContextPool.Get();
	ctx->entries.clear();

	int loc = l->index;
	bool active = (world->GetCurrentLocationIndex() == loc);
	assert(l->last_visit != -1);

	switch(l->type)
	{
	case L_CITY:
		{
			City* city = static_cast<City*>(l);
			if(level == -1)
			{
				ctx->entries.resize(city->inside_buildings.size() + 1);
				LevelAreaContext::Entry& e = ctx->entries[0];
				e.active = active;
				e.area = city;
				e.level = -2;
				e.loc = loc;
				for(int i = 0, len = (int)city->inside_buildings.size(); i < len; ++i)
				{
					LevelAreaContext::Entry& e2 = ctx->entries[i + 1];
					e2.active = active;
					e2.area = city->inside_buildings[i];
					e2.level = i;
					e2.loc = loc;
				}
			}
			else if(level == -2)
			{
				LevelAreaContext::Entry& e = Add1(ctx->entries);
				e.active = active;
				e.area = city;
				e.level = -2;
				e.loc = loc;
			}
			else
			{
				assert(level >= 0 && level < (int)city->inside_buildings.size());
				LevelAreaContext::Entry& e = Add1(ctx->entries);
				e.active = active;
				e.area = city->inside_buildings[level];
				e.level = level;
				e.loc = loc;
			}
		}
		break;
	case L_CAVE:
	case L_DUNGEON:
		{
			InsideLocation* inside = static_cast<InsideLocation*>(l);
			if(inside->IsMultilevel())
			{
				MultiInsideLocation* multi = static_cast<MultiInsideLocation*>(inside);
				if(level == -1)
				{
					ctx->entries.resize(multi->generated);
					for(int i = 0; i < multi->generated; ++i)
					{
						LevelAreaContext::Entry& e = ctx->entries[i];
						e.active = (active && game_level->dungeon_level == i);
						e.area = multi->levels[i];
						e.level = i;
						e.loc = loc;
					}
				}
				else
				{
					assert(level >= 0 && level < multi->generated);
					LevelAreaContext::Entry& e = Add1(ctx->entries);
					e.active = (active && game_level->dungeon_level == level);
					e.area = multi->levels[level];
					e.level = level;
					e.loc = loc;
				}
			}
			else
			{
				assert(level == -1 || level == 0);
				SingleInsideLocation* single = static_cast<SingleInsideLocation*>(inside);
				LevelAreaContext::Entry& e = Add1(ctx->entries);
				e.active = active;
				e.area = single;
				e.level = 0;
				e.loc = loc;
			}
		}
		break;
	case L_OUTSIDE:
	case L_ENCOUNTER:
	case L_CAMP:
		{
			assert(level == -1);
			OutsideLocation* outside = static_cast<OutsideLocation*>(l);
			LevelAreaContext::Entry& e = Add1(ctx->entries);
			e.active = active;
			e.area = outside;
			e.level = -1;
			e.loc = loc;
		}
		break;
	default:
		assert(0);
		break;
	}
}

//=================================================================================================
ForLocation::~ForLocation()
{
	LevelAreaContextPool.Free(ctx);
}

//=================================================================================================
GroundItem* LevelAreaContext::FindQuestGroundItem(int quest_id, LevelAreaContext::Entry** entry, int* item_index)
{
	for(LevelAreaContext::Entry& e : entries)
	{
		for(int i = 0, len = (int)e.area->items.size(); i < len; ++i)
		{
			GroundItem* it = e.area->items[i];
			if(it->item->IsQuest(quest_id))
			{
				if(entry)
					*entry = &e;
				if(item_index)
					*item_index = i;
				return it;
			}
		}
	}

	return nullptr;
}

//=================================================================================================
// search only alive enemies for now
Unit* LevelAreaContext::FindUnitWithQuestItem(int quest_id, LevelAreaContext::Entry** entry, int* unit_index, int* item_iindex)
{
	for(LevelAreaContext::Entry& e : entries)
	{
		for(int i = 0, len = (int)e.area->units.size(); i < len; ++i)
		{
			Unit* unit = e.area->units[i];
			if(unit->IsAlive() && unit->IsEnemy(*game->pc->unit))
			{
				int iindex = unit->FindQuestItem(quest_id);
				if(iindex != Unit::INVALID_IINDEX)
				{
					if(entry)
						*entry = &e;
					if(unit_index)
						*unit_index = i;
					if(item_iindex)
						*item_iindex = iindex;
					return unit;
				}
			}
		}
	}

	return nullptr;
}

//=================================================================================================
bool LevelAreaContext::FindUnit(Unit* unit, LevelAreaContext::Entry** entry, int* unit_index)
{
	assert(unit);

	for(LevelAreaContext::Entry& e : entries)
	{
		for(int i = 0, len = (int)e.area->units.size(); i < len; ++i)
		{
			Unit* unit2 = e.area->units[i];
			if(unit == unit2)
			{
				if(entry)
					*entry = &e;
				if(unit_index)
					*unit_index = i;
				return true;
			}
		}
	}

	return false;
}

//=================================================================================================
Unit* LevelAreaContext::FindUnit(UnitData* data, LevelAreaContext::Entry** entry, int* unit_index)
{
	assert(data);

	for(LevelAreaContext::Entry& e : entries)
	{
		for(int i = 0, len = (int)e.area->units.size(); i < len; ++i)
		{
			Unit* unit = e.area->units[i];
			if(unit->data == data)
			{
				if(entry)
					*entry = &e;
				if(unit_index)
					*unit_index = i;
				return unit;
			}
		}
	}

	return nullptr;
}

//=================================================================================================
Unit* LevelAreaContext::FindUnit(delegate<bool(Unit*)> clbk, LevelAreaContext::Entry** entry, int* unit_index)
{
	for(LevelAreaContext::Entry& e : entries)
	{
		for(int i = 0, len = (int)e.area->units.size(); i < len; ++i)
		{
			Unit* unit2 = e.area->units[i];
			if(clbk(unit2))
			{
				if(entry)
					*entry = &e;
				if(unit_index)
					*unit_index = i;
				return unit2;
			}
		}
	}

	return nullptr;
}

//=================================================================================================
bool LevelAreaContext::RemoveQuestGroundItem(int quest_id)
{
	LevelAreaContext::Entry* entry;
	int index;
	GroundItem* item = FindQuestGroundItem(quest_id, &entry, &index);
	if(item)
	{
		if(entry->active && Net::IsOnline())
		{
			NetChange& c = Add1(Net::changes);
			c.type = NetChange::REMOVE_ITEM;
			c.id = item->id;
		}
		RemoveElementIndex(entry->area->items, index);
		return true;
	}
	else
		return false;
}

//=================================================================================================
// search only alive enemies for now
bool LevelAreaContext::RemoveQuestItemFromUnit(int quest_id)
{
	LevelAreaContext::Entry* entry;
	int item_iindex;
	Unit* unit = FindUnitWithQuestItem(quest_id, &entry, nullptr, &item_iindex);
	if(unit)
	{
		unit->RemoveItem(item_iindex, entry->active);
		return true;
	}
	else
		return false;
}

//=================================================================================================
// only remove alive units for now
bool LevelAreaContext::RemoveUnit(Unit* unit)
{
	assert(unit);

	LevelAreaContext::Entry* entry;
	int unit_index;
	if(FindUnit(unit, &entry, &unit_index))
	{
		if(entry->active)
		{
			unit->to_remove = true;
			game_level->to_remove.push_back(unit);
		}
		else
			RemoveElementIndex(entry->area->units, unit_index);
		return true;
	}
	else
		return false;
}
