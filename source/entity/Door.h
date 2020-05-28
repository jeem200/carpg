#pragma once

//-----------------------------------------------------------------------------
enum LockId
{
	LOCK_NONE,
	LOCK_UNLOCKABLE,
	LOCK_MINE,
	LOCK_ORCS,
	LOCK_TUTORIAL = 100
};

//-----------------------------------------------------------------------------
struct Door : public EntityType<Door>
{
	enum State
	{
		Closed,
		Opening,
		Opening2,
		Opened,
		Closing,
		Closing2,
		Max
	};

	static const float WIDTH;
	static const float THICKNESS;
	static const float HEIGHT;
	static const float SOUND_DIST;
	static const float UNLOCK_SOUND_DIST;
	static const float BLOCKED_SOUND_DIST;
	static const int MIN_SIZE = 31;

	MeshInstance* mesh_inst;
	btCollisionObject* phy;
	Vec3 pos;
	float rot;
	Int2 pt;
	State state;
	int locked;
	bool door2;

	Door() : door2(false), mesh_inst(nullptr)
	{
	}
	~Door();
	bool IsBlocking() const { return Any(state, Closed, Opening, Closing2); }
	bool IsBlockingView() const { return state == Closed; }
	bool IsAnimated() const { return Any(state, Opening, Opening2, Closing, Closing2); }
	void Save(GameWriter& f);
	void Load(GameReader& f);
	void Write(BitStreamWriter& f);
	bool Read(BitStreamReader& f);
	Vec3 GetCenter() const
	{
		Vec3 p = pos;
		p.y += 1.5f;
		return p;
	}
	void Open();
	void Close();
};
