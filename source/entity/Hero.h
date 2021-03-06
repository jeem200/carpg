#pragma once

//-----------------------------------------------------------------------------
#include "HeroPlayerCommon.h"

//-----------------------------------------------------------------------------
enum class HeroType
{
	Normal,
	Free, // no gold shares
	Visitor // no gold/exp
};

//-----------------------------------------------------------------------------
struct Hero : public HeroPlayerCommon
{
	int expe;
	float phase_timer;
	HeroType type;
	AITeam* otherTeam;
	bool know_name, team_member, lost_pvp, melee, phase, gained_gold, loner;

	void Init(Unit& unit);
	int JoinCost() const;
	void Save(GameWriter& f);
	void Load(GameReader& f);
	void PassTime(int days = 1, bool travel = false);
	void SetupMelee();
	void AddExp(int exp);
	float GetExpMod() const;
	bool HaveOtherTeam() const { return otherTeam != nullptr; }
	int GetPersuasionCheckValue() const;
	bool WantJoin() const;

private:
	void LevelUp();
};
