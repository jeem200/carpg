#pragma once

//-----------------------------------------------------------------------------
#include "Collision.h"
#include "Bullet.h"
#include "Blood.h"
#include "Drain.h"
#include "GameLight.h"
#include <Mesh.h>

//-----------------------------------------------------------------------------
struct LightMask
{
	Vec2 pos, size;
};

//-----------------------------------------------------------------------------
// pre V_0_11 compatibility
namespace old
{
	enum class LoadCompatibility
	{
		None,
		InsideBuilding,
		InsideLocationLevel,
		InsideLocationLevelTraps,
		OutsideLocation
	};
}

//-----------------------------------------------------------------------------
// Part of Location Level (outside, building or dungeon level)
struct LevelArea
{
	enum class Type
	{
		Outside,
		Inside,
		Building
	};

	static const int OUTSIDE_ID = -1;
	static const int OLD_EXIT_ID = -2;

	const int area_id; // -1 outside, 0+ building or dungeon level
	const Type area_type;
	TmpLevelArea* tmp;
	vector<Unit*> units;
	vector<Object*> objects;
	vector<Usable*> usables;
	vector<Door*> doors;
	vector<Chest*> chests;
	vector<GroundItem*> items;
	vector<Trap*> traps;
	vector<Blood> bloods;
	vector<GameLight> lights;
	vector<LightMask> masks;
	Int2 mine, maxe;
	const bool have_terrain;

	LevelArea(Type area_type, int area_id, bool have_terrain) : area_type(area_type), area_id(area_id), have_terrain(have_terrain), tmp(nullptr) {}
	~LevelArea();
	void Update(float dt);
	void Save(GameWriter& f);
	void Load(GameReader& f, old::LoadCompatibility compatibility = old::LoadCompatibility::None);
	void Write(BitStreamWriter& f);
	bool Read(BitStreamReader& f);
	void Clear();
	cstring GetName();
	Unit* FindUnit(UnitData* ud);
	Usable* FindUsable(BaseUsable* base);
	bool RemoveItem(const Item* item);
	bool FindItemInCorpse(const Item* item, Unit** unit, int* slot);
	bool RemoveGroundItem(const Item* item);
	Object* FindObject(BaseObject* base_obj);
	Object* FindNearestObject(BaseObject* base_obj, const Vec3& pos);
	Chest* FindChestInRoom(const Room& p);
	Chest* GetRandomFarChest(const Int2& pt);
	bool HaveUnit(Unit* unit);
	Chest* FindChestWithItem(const Item* item, int* index);
	Chest* FindChestWithQuestItem(int quest_id, int* index);
	bool RemoveItemFromChest(const Item* item);
	Door* FindDoor(const Int2& pt);
	bool IsActive() const { return tmp != nullptr; }
	void SpellHitEffect(Bullet& bullet, const Vec3& pos, Unit* hitted);
	bool CheckForHit(Unit& unit, Unit*& hitted, Vec3& hitpoint);
	bool CheckForHit(Unit& unit, Unit*& hitted, Mesh::Point& hitbox, Mesh::Point* bone, Vec3& hitpoint);
	Explo* CreateExplo(Ability* ability, const Vec3& pos);
};

//-----------------------------------------------------------------------------
// Temporary level area (used only for active level areas to hold temporary entities)
struct TmpLevelArea : ObjectPoolProxy<TmpLevelArea>
{
	LevelArea* area;
	vector<Bullet*> bullets;
	vector<ParticleEmitter*> pes;
	vector<TrailParticleEmitter*> tpes;
	vector<Explo*> explos;
	vector<Electro*> electros;
	vector<Drain> drains;
	vector<CollisionObject> colliders;
	float lights_dt;

	void Clear();
	void Save(GameWriter& f);
	void Load(GameReader& f);
	void Write(BitStreamWriter& f);
	bool Read(BitStreamReader& f);
};
