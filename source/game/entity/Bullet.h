// pocisk (strza�a, czar)
#pragma once

//-----------------------------------------------------------------------------
#include "Resource.h"

//-----------------------------------------------------------------------------
struct Unit;
struct Spell;
struct ParticleEmitter;
struct TrailParticleEmitter;

//-----------------------------------------------------------------------------
struct Bullet
{
	VEC3 pos, rot, start_pos;
	Animesh* mesh;
	float speed, timer, attack, tex_size, yspeed, poison_attack;
	int level, backstab;
	Unit* owner;
	Spell* spell;
	Texture tex;
	TrailParticleEmitter* trail, *trail2;
	ParticleEmitter* pe;
	bool remove;

	void Save(File& f);
	void Load(File& f);
};
