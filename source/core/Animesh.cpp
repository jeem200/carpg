#include "Pch.h"
#include "Base.h"
#include "Animesh.h"
#include "Engine.h"
#include "SaveState.h"
#include "BitStreamFunc.h"

//---------------------------
Animesh::KeyframeBone blendb_zero;
MATRIX mat_zero;
void (*AnimeshInstance::Predraw)(void*,MATRIX*,int) = NULL;

struct AVertex
{
	VEC3 pos;
	VEC3 normal;
	VEC2 tex;
	VEC3 tangent;
	VEC3 binormal;
};

const VEC3 DefaultSpecularColor(1,1,1);
const float DefaultSpecularIntensity = 0.2f;
const int DefaultSpecularHardness = 10;

//=================================================================================================
// Inicjalizacja obiekt�w u�ywanych do animacji modeli
//=================================================================================================
int MeshInit()
{
	blendb_zero.scale = 1.f;
	D3DXQuaternionIdentity(&blendb_zero.rot);

	MATRIX tmp;
	D3DXMatrixScaling( &mat_zero, blendb_zero.scale );
	D3DXMatrixRotationQuaternion( &tmp, &blendb_zero.rot );
	mat_zero *= tmp;
	D3DXMatrixTranslation( &tmp, blendb_zero.pos );
	mat_zero *= tmp;

	return 0;
};

//=================================================================================================
// Konstruktor Animesh
//=================================================================================================
Animesh::Animesh() : vb(NULL), ib(NULL)
{
}

//=================================================================================================
// Destruktor Animesh
//=================================================================================================
Animesh::~Animesh()
{
	SafeRelease(vb);
	SafeRelease(ib);
}

//=================================================================================================
// Wczytywanie modelu z pliku
//=================================================================================================
void Animesh::Load(HANDLE file, IDirect3DDevice9* device)
{
	assert(device);

	DWORD tmp;
	byte length;

	// nag��wek
	ReadFile(file, &head, sizeof(head), &tmp, NULL);
	if(tmp != sizeof(head))
		throw "Failed to read file header!";
	if(memcmp(head.format,"QMSH",4) != 0)
		throw Format("Invalid file signature '%.4s'!", head.format);

	// sprawd� wersj�
	if(head.version < 12 || head.version > 19)
		throw Format("Invalid file version '%d'!", head.version);

	// wczytaj ilo�� ko�ci odkszta�caj�cych model
	/*if(head.version == 13)
	{
		ReadFile(file, &n_real_bones, 4, &tmp, NULL);
		if(tmp != 4)
			throw "B��d odczytu liczby ko�ci!";
	}
	else
		n_real_bones = head.n_bones;*/

	// kamera
	if(head.version >= 13)
	{
		ReadFile(file, &cam_pos, sizeof(cam_pos), &tmp, NULL);
		ReadFile(file, &cam_target, sizeof(cam_target), &tmp, NULL);
		if(head.version >= 15)
			ReadFile(file, &cam_up, sizeof(cam_up), &tmp, NULL);
		else
			cam_up = VEC3(0,1,0);
	}
	else
	{
		cam_pos = VEC3(1,1,1);
		cam_target = VEC3(0,0,0);
		cam_up = VEC3(0,1,0);
	}

	// sprawd� poprawno�� modelu
	if(head.n_bones /*n_real_bones*/ >= 32)
		throw Format("Too many bones (%d)!", head.n_bones /*n_real_bones*/);
	if(head.n_subs == 0)
		throw "Missing model mesh!";
	if(IS_SET(head.flags,ANIMESH_ANIMATED) && !IS_SET(head.flags,ANIMESH_STATIC))
	{
		if(head.n_bones /*n_real_bones*/ == 0)
			throw "No bones!";
		if(head.version >= 13 && head.n_groups == 0)
			throw "No bone groups!";
	}

	// wczytaj wierzcho�ki
	{
		// ustal rozmiar wierzcho�ka i fvf
		if(IS_SET(head.flags, ANIMESH_PHYSICS))
		{
			vertex_decl = VDI_POS;
			vertex_size = sizeof(VPos);
		}
		else
		{
			vertex_size = sizeof(VEC3);
			if(IS_SET(head.flags,ANIMESH_ANIMATED))
			{
				if(IS_SET(head.flags,ANIMESH_TANGENTS))
				{
					vertex_decl = VDI_ANIMATED_TANGENT;
					vertex_size = sizeof(VAnimatedTangent);
				}
				else
				{
					vertex_decl = VDI_ANIMATED;
					vertex_size = sizeof(VAnimated);
				}
			}
			else
			{
				if(IS_SET(head.flags,ANIMESH_TANGENTS))
				{
					vertex_decl = VDI_TANGENT;
					vertex_size = sizeof(VTangent);
				}
				else
				{
					vertex_decl = VDI_DEFAULT;
					vertex_size = sizeof(VDefault);
				}
			}
		}

		const dword size = vertex_size * head.n_verts;

		// stw�rz bufor wierzcho�k�w
		HRESULT hr = device->CreateVertexBuffer(size, 0, 0, D3DPOOL_MANAGED, &vb, NULL);
		if(FAILED(hr))
			throw Format("Failed to create vertex buffer (%d)!", hr);

		// zablokuj i wczytaj
		void* ptr;
		vb->Lock(0, size, &ptr, 0);
		ReadFile(file, ptr, size, &tmp, NULL);
		vb->Unlock();

		if(tmp != size)
			throw "Failed to read vertex buffer!";
	}

	// wczytaj tr�jk�ty
	{
		const dword tris_size = sizeof(word) * head.n_tris * 3;

		// stw�rz bufor indeks�w
		HRESULT hr = device->CreateIndexBuffer(tris_size, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL);
		if(FAILED(hr))
			throw Format("Failed to create index buffer (%d)!", hr);

		// zablokuj i wczytaj
		void* ptr;
		ib->Lock(0, tris_size, &ptr, 0);
		ReadFile(file, ptr, tris_size, &tmp, NULL);
		ib->Unlock();

		if(tmp != tris_size)
			throw "Failed to read index buffer!";
	}

	// wczytaj submodele
	{
		subs.resize(head.n_subs);

		for(word i=0; i<head.n_subs; ++i)
		{
			Animesh::Submesh& sub = subs[i];

			ReadFile(file, &sub.first,   2, &tmp, NULL);
			ReadFile(file, &sub.tris,    2, &tmp, NULL);
			ReadFile(file, &sub.min_ind, 2, &tmp, NULL);
			ReadFile(file, &sub.n_ind,   2, &tmp, NULL);

			// nazwa
			ReadFile(file, &length, 1, &tmp, NULL);
			sub.name.resize(length);
			ReadFile(file, (void*)sub.name.c_str(), length, &tmp, NULL);

			// tekstura
			ReadFile(file, &length, 1, &tmp, NULL);
			if(length != 0)
			{
				ReadFile(file, BUF, length, &tmp, NULL);
				BUF[length] = 0;

				sub.tex = Engine::_engine->LoadTexResource(BUF);
			}
			else
				sub.tex = NULL;

			if(head.version >= 16)
			{
				FileReader f(file);

				if(head.version >= 18)
				{
					f >> sub.specular_color;
					f >> sub.specular_intensity;
					f >> sub.specular_hardness;
				}
				else
				{
					sub.specular_color = DefaultSpecularColor;
					sub.specular_intensity = DefaultSpecularIntensity;
					sub.specular_hardness = DefaultSpecularHardness;
				}

				// normalmapa
				if(IS_SET(head.flags, ANIMESH_TANGENTS))
				{
					f.ReadStringBUF();
					if(BUF[0])
					{
						sub.tex_normal = Engine::_engine->LoadTexResource(BUF);
						f >> sub.normal_factor;
					}
					else
						sub.tex_normal = NULL;
				}
				else
					sub.tex_normal = NULL;

				// specular
				f.ReadStringBUF();
				if(BUF[0])
				{
					sub.tex_specular = Engine::_engine->LoadTexResource(BUF);
					f >> sub.specular_factor;
					f >> sub.specular_color_factor;
				}
				else
					sub.tex_specular = NULL;
			}
			else
			{
				sub.tex_specular = NULL;
				sub.tex_normal = NULL;
				sub.specular_color = DefaultSpecularColor;
				sub.specular_intensity = DefaultSpecularIntensity;
				sub.specular_hardness = DefaultSpecularHardness;
			}
		}
	}
	
	if(IS_SET(head.flags,ANIMESH_ANIMATED) && !IS_SET(head.flags,ANIMESH_STATIC))
	{
		// ko�ci
		{
			bones.resize(head.n_bones+1);

			// ko�� zerowa
			Animesh::Bone& zero_bone = bones[0];
			zero_bone.parent = 0;
			zero_bone.name = "zero";
			zero_bone.id = 0;
			D3DXMatrixIdentity(&zero_bone.mat);

			for(byte i=1; i<=head.n_bones; ++i)
			{
				Animesh::Bone& bone = bones[i];

				bone.id = i;
				ReadFile( file, &bone.parent, 2, &tmp, NULL );

				ReadFile( file, &bone.mat._11, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._12, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._13, 4, &tmp, NULL );
				bone.mat._14 = 0;
				ReadFile( file, &bone.mat._21, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._22, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._23, 4, &tmp, NULL );
				bone.mat._24 = 0;
				ReadFile( file, &bone.mat._31, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._32, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._33, 4, &tmp, NULL );
				bone.mat._34 = 0;
				ReadFile( file, &bone.mat._41, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._42, 4, &tmp, NULL );
				ReadFile( file, &bone.mat._43, 4, &tmp, NULL );
				bone.mat._44 = 1;

				//ReadFile(file, &bone.pos, sizeof(bone.pos), &tmp, NULL);

				ReadFile(file, &length, 1, &tmp, NULL);
				bone.name.resize(length);
				ReadFile(file, (void*)bone.name.c_str(), length, &tmp, NULL);

				bones[bone.parent].childs.push_back(i);
			}
		}

		// wczytaj animacje
		{
			anims.resize(head.n_anims);

			for(byte i=0; i<head.n_anims; ++i)
			{
				Animesh::Animation& anim = anims[i];
				
				ReadFile(file, &length, 1, &tmp, NULL);
				anim.name.resize(length);
				ReadFile(file, (void*)anim.name.c_str(), length, &tmp, NULL);

				ReadFile( file, &anim.length, 4, &tmp, NULL );
				ReadFile( file, &anim.n_frames, 2, &tmp, NULL );

				anim.frames.resize(anim.n_frames);

				for(word j=0; j<anim.n_frames; ++j)
				{
					ReadFile(file, &anim.frames[j].time, 4, &tmp, NULL);
					anim.frames[j].bones.resize(head.n_bones);
					ReadFile(file, &anim.frames[j].bones[0], sizeof(Animesh::KeyframeBone) * head.n_bones, &tmp, NULL);
				}
			}

			// dodaj zerow� ko�� do liczby ko�ci
			++head.n_bones;
			//++n_real_bones;
		}
	}

	// wczytaj punkty
	if(head.n_points > 0)
	{
		FileReader f(file);
		attach_points.resize(head.n_points);
		for(word i=0; i<head.n_points; ++i)
		{
			Point& p = attach_points[i];

			f >> p.name;
			f >> p.mat;
			f >> p.bone;
			if(head.version == 12)
				++p.bone; // tu trzeba doda� bo tam liczy od zera
			f >> p.type;
			if(head.version >= 14)
				f >> p.size;
			else
			{
				f >> p.size.x;
				p.size.y = p.size.z = p.size.x;
			}
			if(head.version >= 19)
			{
				f >> p.rot;
				p.rot.y = clip(-p.rot.y);
			}
			else
			{
				// fallback, zazwyczaj nie dzia�a ale wcze�niej tak by�o (dobrze dzia�a dla PI/2 i PI*3/2, dla 0 i PI jest odwr�cone, dla reszty �le)
				p.rot = VEC3(0,MatrixGetYaw(p.mat),0);
			}
		}
	}

	if(IS_SET(head.flags,ANIMESH_ANIMATED) && !IS_SET(head.flags,ANIMESH_STATIC))
	{
		// grupy
		if(head.version == 12 && head.n_groups < 2)
		{
			head.n_groups = 1;
			groups.resize(1);
			
			Animesh::BoneGroup& gr = groups[0];
			gr.name = "default";
			gr.parent = 0;
			gr.bones.reserve(head.n_bones-1);

			for(word i=1; i<head.n_bones; ++i)
				gr.bones.push_back(i);
		}
		else
		{
			groups.resize(head.n_groups);
			for(word i=0; i<head.n_groups; ++i)
			{
				Animesh::BoneGroup& gr = groups[i];

				// nazwa
				ReadFile(file, &length, 1, &tmp, NULL);
				gr.name.resize(length);
				ReadFile(file, (void*)gr.name.c_str(), length, &tmp, NULL);

				// nadrz�dna grupa
				ReadFile(file, &gr.parent, 2, &tmp, NULL);
				assert(gr.parent < head.n_groups);
				assert(gr.parent != i || i == 0);

				// ko�ci
				byte ile;
				ReadFile(file, &ile, 1, &tmp, NULL);
				gr.bones.reserve(ile);

				for(byte j=0; j<ile; ++j)
				{
					byte id;
					ReadFile(file,&id,1,&tmp,NULL);
					if(head.version == 12)
						++id;
					gr.bones.push_back(id);
				}
			}
		}

		SetupBoneMatrices();
	}

	if(IS_SET(head.flags, ANIMESH_SPLIT))
	{
		splits.resize(head.n_subs);
		ReadFile(file, &splits[0], sizeof(Split)*head.n_subs, &tmp, NULL);
	}
}

//=================================================================================================
// Ustawienie macierzy ko�ci
//=================================================================================================
void Animesh::SetupBoneMatrices()
{
	model_to_bone.resize(head.n_bones);
	D3DXMatrixIdentity(&model_to_bone[0]);

	for(word i=1; i<head.n_bones; ++i)
	{
		const Animesh::Bone& bone = bones[i];

		D3DXMatrixInverse(&model_to_bone[i], NULL, &bone.mat);

		if(bone.parent > 0)
			D3DXMatrixMultiply(&model_to_bone[i], &model_to_bone[bone.parent], &model_to_bone[i]);
	}
}

//=================================================================================================
// Zwraca ko�� o podanej nazwie
//=================================================================================================
Animesh::Bone* Animesh::GetBone(cstring name)
{
	assert(name);

	for(vector<Bone>::iterator it = bones.begin(), end = bones.end(); it != end; ++it)
	{
		if(it->name == name)
			return &*it;
	}
	
	return NULL;
}

//=================================================================================================
// Zwraca animacj� o podanej nazwie
//=================================================================================================
Animesh::Animation* Animesh::GetAnimation(cstring name)
{
	assert(name);

	for(vector<Animation>::iterator it = anims.begin(), end = anims.end(); it != end; ++it)
	{
		if(it->name == name)
			return &*it;
	}

	return NULL;
}

//=================================================================================================
// Zwraca indeks ramki i czy dok�adne trafienie
//=================================================================================================
int Animesh::Animation::GetFrameIndex( float time, bool& hit )
{
	//assert_return(time >= 0 && time <= length && "Czas poza animacj�", 0);

	for( word i=0; i<n_frames; ++i )
	{
		if( equal( time, frames[i].time ) )
		{
			// r�wne trafienie w klatk�
			hit = true;
			return i;
		}
		else if( time < frames[i].time )
		{
			// b�dzie potrzebna interpolacja mi�dzy dwoma klatkami
			assert( i != 0 && "Czas przed pierwsz� klatk�!" );
			hit = false;
			return i - 1;
		}
	}

	// b��d, chyba nie mo�e tu doj�� bo by wywali�o si� na assercie
	// chyba �e w trybie release
	return -1;
}

//=================================================================================================
// Interpolacja skali, pozycji i obrotu
//=================================================================================================
void Animesh::KeyframeBone::Interpolate( Animesh::KeyframeBone& out, const Animesh::KeyframeBone& k,
									  const Animesh::KeyframeBone& k2, float t )
{
	D3DXQuaternionSlerp( &out.rot, &k.rot, &k2.rot, t );
	D3DXVec3Lerp( &out.pos, &k.pos, &k2.pos, t );
	out.scale = lerp( k.scale, k2.scale, t );
}

//=================================================================================================
// Mno�enie macierzy w przekszta�ceniu dla danej ko�ci
//=================================================================================================
void Animesh::KeyframeBone::Mix( MATRIX& out, const MATRIX& mul ) const
{
	MATRIX tmp;

	D3DXMatrixScaling( &out, scale );
	D3DXMatrixRotationQuaternion( &tmp, &rot );
	out *= tmp;
	D3DXMatrixTranslation( &tmp, pos );
	out *= tmp;
	out *= mul;
}

//=================================================================================================
// Zwraca punkt o podanej nazwie
//=================================================================================================
Animesh::Point* Animesh::GetPoint(cstring name)
{
	assert(name);

	for(vector<Point>::iterator it = attach_points.begin(), end = attach_points.end(); it != end; ++it)
	{
		if(it->name == name)
			return &*it;
	}

	return NULL;
}

//=================================================================================================
// Zwraca dane ramki
//=================================================================================================
void Animesh::GetKeyframeData(KeyframeBone& keyframe, Animation* anim, uint bone, float time)
{
	assert(anim);

	bool hit;
	int index = anim->GetFrameIndex(time, hit);

	if(hit)
	{
		// exact hit in frame
		keyframe = anim->frames[index].bones[bone-1];
	}
	else
	{
		// interpolate beetween two key frames
		const vector<Animesh::KeyframeBone>& keyf = anim->frames[index].bones;
		const vector<Animesh::KeyframeBone>& keyf2 = anim->frames[index+1].bones;
		const float t = (time - anim->frames[index].time) /
			(anim->frames[index+1].time - anim->frames[index].time);

		KeyframeBone::Interpolate(keyframe, keyf[bone-1], keyf2[bone-1], t);
	}
}

//=================================================================================================
// Konstruktor instancji Animesh
//=================================================================================================
AnimeshInstance::AnimeshInstance(Animesh* ani) : ani(ani), need_update(true), frame_end_info(false),
frame_end_info2(false), /*sub_override(NULL),*/ ptr(NULL)
{
	assert(ani);

	mat_bones.resize(ani->head.n_bones);
	blendb.resize(ani->head.n_bones);
	groups.resize(ani->head.n_groups);
}

//=================================================================================================
// Destruktor instancji Animesh
//=================================================================================================
AnimeshInstance::~AnimeshInstance()
{
	/*if(sub_override)
	{
		for(word i=0; i<ani->head.n_subs; ++i)
		{
			if(sub_override[i].tex)
				sub_override[i].tex->Release();
		}
		delete[] sub_override;
	}*/
}

//=================================================================================================
// Odtwarza podan� animacj�, szybsze bo nie musi szuka� nazwy
//=================================================================================================
void AnimeshInstance::Play(Animesh::Animation* anim, int flags, int group)
{
	assert(anim && in_range(group, 0, ani->head.n_groups-1));

	Group& gr = groups[group];

	// ignoruj animacj�
	if(IS_SET(flags, PLAY_IGNORE) && gr.anim == anim)
		return;

	// resetuj szybko�� i blending
	if(IS_SET(gr.state, PLAY_RESTORE))
	{
		gr.speed = 1.f;
		gr.blend_max = 0.33f;
	}

	int new_state = 0;

	// blending
	if(!IS_SET(flags, PLAY_NO_BLEND))
	{
		SetupBlending(group);
		SET_BIT(new_state, FLAG_BLENDING);
		if(IS_SET(flags, PLAY_BLEND_WAIT))
			SET_BIT(new_state, FLAG_BLEND_WAIT);
		gr.blend_time = 0.f;
	}

	// ustaw animacj�
	gr.anim = anim;
	gr.prio = ((flags & 0x60)>>5);
	gr.state = new_state | FLAG_PLAYING | FLAG_GROUP_ACTIVE;
	if(IS_SET(flags, PLAY_ONCE))
		SET_BIT(gr.state, FLAG_ONCE);
	if(IS_SET(flags, PLAY_BACK))
	{
		SET_BIT(gr.state, FLAG_BACK);
		gr.time = anim->length;
	}
	else
		gr.time = 0.f;
	if(IS_SET(flags, PLAY_STOP_AT_END))
		SET_BIT(gr.state, FLAG_STOP_AT_END);
	if(IS_SET(flags, PLAY_RESTORE))
		SET_BIT(gr.state, FLAG_RESTORE);

	// anuluj blending w innych grupach
	if(IS_SET(flags, PLAY_NO_BLEND))
	{
		for(int g=0; g<ani->head.n_groups; ++g)
		{
			if(g != group && (!groups[g].IsActive() || groups[g].prio < gr.prio))
				CLEAR_BIT(groups[g].state, FLAG_BLENDING);
		}
	}
}

//=================================================================================================
// Wy��cz grup�
//=================================================================================================
void AnimeshInstance::Deactivate(int group)
{
	assert(in_range(group, 0, ani->head.n_groups-1));

	Group& gr = groups[group];

	if(IS_SET(gr.state, FLAG_GROUP_ACTIVE))
	{
		SetupBlending(group);

		if(IS_SET(gr.state, FLAG_RESTORE))
		{
			gr.speed = 1.f;
			gr.blend_max = 0.33f;
		}

		gr.state = FLAG_BLENDING;
		gr.blend_time = 0.f;
	}
}
//=================================================================================================
// Aktualizuje animacje
//=================================================================================================
void AnimeshInstance::Update(float dt)
{
	frame_end_info = false;
	frame_end_info2 = false;

	for( word i=0; i<ani->head.n_groups; ++i )
	{
		Group& gr = groups[i];

		// blending
		if( IS_SET(gr.state,FLAG_BLENDING) )
		{
			need_update = true;
			gr.blend_time += dt;
			if(gr.blend_time >= gr.blend_max)
				CLEAR_BIT(gr.state,FLAG_BLENDING);
		}

		// odtwarzaj animacj�
		if( IS_SET(gr.state,FLAG_PLAYING))
		{
			need_update = true;

			if(IS_SET(gr.state, FLAG_BLEND_WAIT))
			{
				if(IS_SET(gr.state, FLAG_BLENDING))
					continue;
			}

			// odtwarzaj od ty�u
			if( IS_SET(gr.state,FLAG_BACK) )
			{
				gr.time -= dt * gr.speed;
				if( gr.time < 0 ) // przekroczono czas animacji
				{
					// informacja o ko�cu animacji (do wywalenia)
					if( i == 0 )
						frame_end_info = true;
					else
						frame_end_info2 = true;
					if( IS_SET(gr.state,FLAG_ONCE) )
					{
						gr.time = 0;
						if( IS_SET(gr.state,FLAG_STOP_AT_END) )
							Stop(i);
						else
							Deactivate(i);
					}
					else
					{
						gr.time += gr.anim->length;
						if( gr.anim->n_frames == 1 )
						{
							gr.time = 0;
							Stop(i);
						}
					}
				}
			}
			else // odtwarzaj normalnie
			{
				gr.time += dt * gr.speed;
				if( gr.time >= gr.anim->length ) // przekroczono czas animacji
				{
					if( i == 0 )
						frame_end_info = true;
					else
						frame_end_info2 = true;
					if( IS_SET(gr.state,FLAG_ONCE) )
					{
						gr.time = gr.anim->length;
						if( IS_SET(gr.state,FLAG_STOP_AT_END) )
							Stop(i);
						else
							Deactivate(i);
					}
					else
					{
						gr.time -= gr.anim->length;
						if( gr.anim->n_frames == 1 )
						{
							gr.time = 0;
							Stop(i);
						}
					}
				}
			}
		}
	}
}

#define BLEND_TO_BIND_POSE -1

//====================================================================================================
// Ustawia ko�ci przed rysowaniem modelu
//====================================================================================================
void AnimeshInstance::SetupBones(MATRIX* mat_scale)
{
	if(!need_update)
		return;
	need_update = false;

	MATRIX BoneToParentPoseMat[32];
	D3DXMatrixIdentity( &BoneToParentPoseMat[0] );
	Animesh::KeyframeBone tmp_keyf;

	// oblicz przekszta�cenia dla ka�dej grupy
	const word n_groups = ani->head.n_groups;
	for(word bones_group=0; bones_group<n_groups; ++bones_group )
	{
		const Group& gr_bones = groups[bones_group];
		const std::vector<word>& bones = ani->groups[bones_group].bones;
		int anim_group;

		// ustal z kt�r� animacj� ustala� blending
		anim_group = GetUseableGroup(bones_group);

		if( anim_group == BLEND_TO_BIND_POSE )
		{
			// nie ma �adnej animacji
			if( gr_bones.IsBlending() )
			{
				// jest blending pomi�dzy B--->0
				float bt = gr_bones.blend_time / gr_bones.blend_max;

				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					Animesh::KeyframeBone::Interpolate( tmp_keyf, blendb[b], blendb_zero, bt );
					tmp_keyf.Mix( BoneToParentPoseMat[b], ani->bones[b].mat );
				}
			}
			else
			{
				// brak blendingu, wszystko na zero
				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					BoneToParentPoseMat[b] = mat_zero * ani->bones[b].mat;
				}
			}
		}
		else
		{
			const Group& gr_anim = groups[anim_group];
			bool hit;
			const int index = gr_anim.GetFrameIndex(hit);
			const vector<Animesh::Keyframe>& frames = gr_anim.anim->frames;

			if( gr_anim.IsBlending() || gr_bones.IsBlending() )
			{
				// jest blending
				const float bt = (gr_bones.IsBlending() ? (gr_bones.blend_time / gr_bones.blend_max) :
					(gr_anim.blend_time / gr_anim.blend_max));

				if(hit)
				{
					// r�wne trafienie w klatk�
					const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
					for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
					{
						const word b = *it;
						Animesh::KeyframeBone::Interpolate( tmp_keyf, blendb[b], keyf[b-1], bt );
						tmp_keyf.Mix( BoneToParentPoseMat[b], ani->bones[b].mat );
					}
				}
				else
				{
					// trzeba interpolowa�
					const float t = (gr_anim.time - frames[index].time) / (frames[index+1].time - frames[index].time);
					const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
					const vector<Animesh::KeyframeBone>& keyf2 = frames[index+1].bones;

					for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
					{
						const word b = *it;
						Animesh::KeyframeBone::Interpolate( tmp_keyf, keyf[b-1], keyf2[b-1], t );
						Animesh::KeyframeBone::Interpolate( tmp_keyf, blendb[b], tmp_keyf, bt );
						tmp_keyf.Mix( BoneToParentPoseMat[b], ani->bones[b].mat );
					}
				}
			}
			else
			{
				// nie ma blendingu
				if(hit)
				{
					// r�wne trafienie w klatk�
					const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
					for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
					{
						const word b = *it;
						keyf[b-1].Mix( BoneToParentPoseMat[b], ani->bones[b].mat );
					}
				}
				else
				{
					// trzeba interpolowa�
					const float t = (gr_anim.time - frames[index].time) / (frames[index+1].time - frames[index].time);
					const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
					const vector<Animesh::KeyframeBone>& keyf2 = frames[index+1].bones;

					for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
					{
						const word b = *it;
						Animesh::KeyframeBone::Interpolate( tmp_keyf, keyf[b-1], keyf2[b-1], t );
						tmp_keyf.Mix( BoneToParentPoseMat[b], ani->bones[b].mat );
					}
				}
			}
		}
	}

	if(ptr)
		Predraw(ptr, BoneToParentPoseMat, 0);

	// Macierze przekszta�caj�ce ze wsp. danej ko�ci do wsp. modelu w ustalonej pozycji
	// (To obliczenie nale�a�oby po��czy� z poprzednim)
	MATRIX BoneToModelPoseMat[32];
	D3DXMatrixIdentity( &BoneToModelPoseMat[0] );
	for( word i=1; i<ani->head.n_bones; ++i )
	{
		const Animesh::Bone& bone = ani->bones[i];

		// Je�li to ko�� g��wna, przekszta�cenie z danej ko�ci do nadrz�dnej = z danej ko�ci do modelu
		// Je�li to nie ko�� g��wna, przekszta�cenie z danej ko�ci do modelu = z danej ko�ci do nadrz�dnej * z nadrz�dnej do modelu
		if( bone.parent == 0 )
			BoneToModelPoseMat[i] = BoneToParentPoseMat[i];
		else
			BoneToModelPoseMat[i] = BoneToParentPoseMat[i] * BoneToModelPoseMat[bone.parent];
	}

	if(ptr)
		Predraw(ptr, BoneToModelPoseMat, 1);

	// przeskaluj ko�ci
	if(mat_scale)
	{
		for(int i=0; i<ani->head.n_bones; ++i)
			D3DXMatrixMultiply(&BoneToModelPoseMat[i], &BoneToModelPoseMat[i], &mat_scale[i]);
	}

	// Macierze zebrane ko�ci - przekszta�caj�ce z modelu do ko�ci w pozycji spoczynkowej * z ko�ci do modelu w pozycji bie��cej
	D3DXMatrixIdentity( &mat_bones[0] );
	for( word i=1; i<ani->head.n_bones; ++i )
		mat_bones[i] = ani->model_to_bone[i] * BoneToModelPoseMat[i];

// 	if(ptr)
// 		Predraw(ptr, &mat_bones[0], 2);
}

//=================================================================================================
// Ustawia blending (przej�cia pomi�dzy animacjami)
//=================================================================================================
void AnimeshInstance::SetupBlending(int bones_group, bool first)
{
	int anim_group;
	const Group& gr_bones = groups[bones_group];
	const std::vector<word>& bones = ani->groups[bones_group].bones;

	// nowe ustalanie z kt�rej grupy bra� animacj�!
	// teraz wybiera wed�ug priorytetu
	anim_group = GetUseableGroup(bones_group);

	if( anim_group == BLEND_TO_BIND_POSE )
	{
		// nie ma �adnej animacji
		if( gr_bones.IsBlending() )
		{
			// jest blending pomi�dzy B--->0
			const float bt = gr_bones.blend_time / gr_bones.blend_max;

			for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
			{
				const word b = *it;
				Animesh::KeyframeBone::Interpolate( blendb[b], blendb[b], blendb_zero, bt );
			}
		}
		else
		{
			// brak blendingu, wszystko na zero
			for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				memcpy( &blendb[*it], &blendb_zero, sizeof(blendb_zero) );
		}
	}
	else
	{
		// jest jaka� animacja
		const Group& gr_anim = groups[anim_group];
		bool hit;
		const int index = gr_anim.GetFrameIndex(hit);
		const vector<Animesh::Keyframe>& frames = gr_anim.anim->frames;

		if( gr_anim.IsBlending() || gr_bones.IsBlending() )
		{
			// je�eli gr_anim == gr_bones to mo�na to zoptymalizowa�

			// jest blending
			const float bt = (gr_bones.IsBlending() ? (gr_bones.blend_time / gr_bones.blend_max) :
				(gr_anim.blend_time / gr_anim.blend_max));

			if(hit)
			{
				// r�wne trafienie w klatk�
				const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					Animesh::KeyframeBone::Interpolate( blendb[b], blendb[b], keyf[b-1], bt );
				}
			}
			else
			{
				// trzeba interpolowa�
				const float t = (gr_anim.time - frames[index].time) / (frames[index+1].time - frames[index].time);
				const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
				const vector<Animesh::KeyframeBone>& keyf2 = frames[index+1].bones;
				Animesh::KeyframeBone tmp_keyf;

				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					Animesh::KeyframeBone::Interpolate( tmp_keyf, keyf[b-1], keyf2[b-1], t );
					Animesh::KeyframeBone::Interpolate( blendb[b], blendb[b], tmp_keyf, bt );
				}
			}
		}
		else
		{
			// nie ma blendingu
			if(hit)
			{
				// r�wne trafienie w klatk�
				const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					blendb[b] = keyf[b-1];
				}
			}
			else
			{
				// trzeba interpolowa�
				const float t = (gr_anim.time - frames[index].time) / (frames[index+1].time - frames[index].time);
				const vector<Animesh::KeyframeBone>& keyf = frames[index].bones;
				const vector<Animesh::KeyframeBone>& keyf2 = frames[index+1].bones;

				for( BoneIter it = bones.begin(), end = bones.end(); it != end; ++it )
				{
					const word b = *it;
					Animesh::KeyframeBone::Interpolate( blendb[b], keyf[b-1], keyf2[b-1], t );
				}
			}
		}
	}

	// znajdz podrz�dne grupy kt�re nie s� aktywne i ustaw im blending
	if(first)
	{
		for( int group=0; group<ani->head.n_groups; ++group )
		{
			if( group != bones_group && (!groups[group].IsActive() ||
				groups[group].prio < gr_bones.prio))
			{
				SetupBlending(group, false);
				SET_BIT( groups[group].state, FLAG_BLENDING );
				groups[group].blend_time = 0;
			}
		}
	}
}

//=================================================================================================
// Czy jest jaki� blending?
//=================================================================================================
bool AnimeshInstance::IsBlending() const
{
	for(int i=0; i<ani->head.n_groups; ++i)
	{
		if(IS_SET(groups[i].state, FLAG_BLENDING))
			return true;
	}
	return false;
}

//=================================================================================================
// Zwraca najwy�szy priorytet animacji i jej grup�
//=================================================================================================
int AnimeshInstance::GetHighestPriority(uint& _group)
{
	int best = -1;

	for(uint i=0; i<uint(ani->head.n_groups); ++i)
	{
		if(groups[i].IsActive() && groups[i].prio > best)
		{
			best = groups[i].prio;
			_group = i;
		}
	}

	return best;
}

//=================================================================================================
// Zwraca grup� kt�rej ma u�ywa� dana grupa
//=================================================================================================
int AnimeshInstance::GetUseableGroup(uint group)
{
	uint top_group;
	int highest_prio = GetHighestPriority(top_group);
	if(highest_prio == -1)
	{
		// brak jakiejkolwiek animacji
		return BLEND_TO_BIND_POSE;
	}
	else if(groups[group].IsActive() && groups[group].prio == highest_prio)
	{
		// u�yj animacji z aktualnej grupy, to mo�e by� r�wnocze�nie 'top_group'
		return group;
	}
	else
	{
		// u�yj animacji z grupy z najwy�szym priorytetem
		return top_group;
	}
}

//=================================================================================================
// Czy�ci ko�ci tak �e jest bazowa poza, pozwala na renderowanie skrzyni bez animacji,
// Przyda�a by si� lepsza nazwa tej funkcji i u�ywanie jej w czasie odtwarzania animacji
//=================================================================================================
void AnimeshInstance::ClearBones()
{
	for(int i=0; i<ani->head.n_bones; ++i)
		D3DXMatrixIdentity(&mat_bones[i]);
	need_update = false;
}

//=================================================================================================
// Ustawia podan� animacje na koniec
//=================================================================================================
void AnimeshInstance::SetToEnd(cstring anim)
{
	assert(anim);

	Animesh::Animation* a = ani->GetAnimation(anim);
	assert(a);

	groups[0].anim = a;
	groups[0].blend_time = 0.f;
	groups[0].state = FLAG_GROUP_ACTIVE;
	groups[0].time = a->length;
	groups[0].used_group = 0;
	groups[0].prio = 3;

	if(ani->head.n_groups > 1)
	{
		for(int i=1; i<ani->head.n_groups; ++i)
		{
			groups[i].anim = NULL;
			groups[i].state = 0;
			groups[i].used_group = 0;
		}
	}
	
	need_update = true;

	SetupBones();

	groups[0].state = 0;
}

//=================================================================================================
// Ustawia podan� animacje na koniec
//=================================================================================================
void AnimeshInstance::SetToEnd(Animesh::Animation* a)
{
	assert(a);

	groups[0].anim = a;
	groups[0].blend_time = 0.f;
	groups[0].state = FLAG_GROUP_ACTIVE;
	groups[0].time = a->length;
	groups[0].used_group = 0;
	groups[0].prio = 3;

	if(ani->head.n_groups > 1)
	{
		for(int i=1; i<ani->head.n_groups; ++i)
		{
			groups[i].anim = NULL;
			groups[i].state = 0;
			groups[i].used_group = 0;
		}
	}

	need_update = true;

	SetupBones();

	groups[0].state = 0;
}

void AnimeshInstance::SetToEnd()
{
	groups[0].blend_time = 0.f;
	groups[0].state = FLAG_GROUP_ACTIVE;
	groups[0].time = groups[0].anim->length;
	groups[0].used_group = 0;
	groups[0].prio = 1;

	for(uint i=1; i<groups.size(); ++i)
	{
		groups[i].state = 0;
		groups[i].used_group = 0;
		groups[i].blend_time = 0;
	}

	need_update = true;

	SetupBones();
}

//=================================================================================================
// Resetuje animacj� z blendingiem
//=================================================================================================
void AnimeshInstance::ResetAnimation()
{
	SetupBlending(0);

	groups[0].time = 0.f;
	groups[0].blend_time = 0.f;
	SET_BIT(groups[0].state, FLAG_BLENDING | FLAG_PLAYING);
}

//=================================================================================================
// Zwraca czas blendingu w przedziale 0..1
//=================================================================================================
float AnimeshInstance::Group::GetBlendT() const
{
	if(IsBlending())
		return blend_time / blend_max;
	else
		return 1.f;
}

//=================================================================================================
// Wczytuje dane wierzcho�k�w z modelu (na razie dzia�a tylko dla VEC3)
//=================================================================================================
VertexData* Animesh::LoadVertexData(HANDLE _file)
{
	DWORD tmp;

	// nag��wek
	Header head;
	ReadFile(_file, &head, sizeof(head), &tmp, NULL);
	if(tmp != sizeof(head))
		throw "Failed to read file header!";
	if(memcmp(head.format,"QMSH",4) != 0)
		throw Format("Invalid file signature '%.4s'!", head.format);

	// sprawd� wersj�
	if(head.version < 13)
		throw Format("Invalid file version '%d'!", head.version);

	// kamera
	SetFilePointer(_file, sizeof(VEC3)*2, NULL, FILE_CURRENT);

	// sprawd� czy to jest fizyka
	if(head.flags != ANIMESH_PHYSICS)
		throw Format("Invalid mesh flags '%d'!", head.flags);

	VertexData* vd = new VertexData;
	vd->radius = head.radius;

	// wczytaj wierzcho�ki
	vd->verts.resize(head.n_verts);
	ReadFile(_file, &vd->verts[0], sizeof(VEC3)*head.n_verts, &tmp, NULL);

	// wczytaj tr�jk�ty
	vd->faces.resize(head.n_tris);
	ReadFile(_file, &vd->faces[0], sizeof(Face)*head.n_tris, &tmp, NULL);

	return vd;
}

//=================================================================================================
Animesh::Point* Animesh::FindPoint(cstring name)
{
	assert(name);

	int len = strlen(name);

	for(vector<Point>::iterator it = attach_points.begin(), end = attach_points.end(); it != end; ++it)
	{
		if(strncmp(name, (*it).name.c_str(), len) == 0)
			return &*it;
	}
	
	return NULL;
}

//=================================================================================================
Animesh::Point* Animesh::FindNextPoint(cstring name, Point* point)
{
	assert(name && point);

	int len = strlen(name);

	for(vector<Point>::iterator it = attach_points.begin(), end = attach_points.end(); it != end; ++it)
	{
		if(&*it == point)
		{
			while(++it != end)
			{
				if(strncmp(name, (*it).name.c_str(), len) == 0)
					return &*it;
			}

			return NULL;
		}
	}

	assert(0);
	return NULL;
}

extern DWORD tmp;
extern char BUF[256];

//=================================================================================================
void AnimeshInstance::Save(HANDLE file)
{
	WriteFile(file, &frame_end_info, sizeof(frame_end_info), &tmp, NULL);
	WriteFile(file, &frame_end_info2, sizeof(frame_end_info2), &tmp, NULL);

	for(vector<Group>::iterator it = groups.begin(), end = groups.end(); it != end; ++it)
	{
		WriteFile(file, &it->time, sizeof(it->time), &tmp, NULL);
		WriteFile(file, &it->speed, sizeof(it->speed), &tmp, NULL);
		// nie zapisuj blendingu
		int state = it->state;
		state &= ~FLAG_BLENDING;
		WriteFile(file, &state, sizeof(state), &tmp, NULL);
		WriteFile(file, &it->prio, sizeof(it->prio), &tmp, NULL);
		WriteFile(file, &it->used_group, sizeof(it->used_group), &tmp, NULL);
		if(it->anim)
		{
			byte len = (byte)it->anim->name.length();
			WriteFile(file, &len, sizeof(len), &tmp, NULL);
			WriteFile(file, it->anim->name.c_str(), len, &tmp, NULL);
		}
		else
		{
			byte len = 0;
			WriteFile(file, &len, sizeof(len), &tmp, NULL);
		}
	}
}

//=================================================================================================
void AnimeshInstance::Load(HANDLE file)
{
	ReadFile(file, &frame_end_info, sizeof(frame_end_info), &tmp, NULL);
	ReadFile(file, &frame_end_info2, sizeof(frame_end_info2), &tmp, NULL);

	for(vector<Group>::iterator it = groups.begin(), end = groups.end(); it != end; ++it)
	{
		ReadFile(file, &it->time, sizeof(it->time), &tmp, NULL);
		ReadFile(file, &it->speed, sizeof(it->speed), &tmp, NULL);
		it->blend_time = 0.f;
		ReadFile(file, &it->state, sizeof(it->state), &tmp, NULL);
		if(LOAD_VERSION < V_0_2_10)
		{
			// unused now
			int last_frame;
			ReadFile(file, &last_frame, sizeof(last_frame), &tmp, NULL);
		}
		ReadFile(file, &it->prio, sizeof(it->prio), &tmp, NULL);
		ReadFile(file, &it->used_group, sizeof(it->used_group), &tmp, NULL);
		byte len;
		ReadFile(file, &len, sizeof(len), &tmp, NULL);
		if(len)
		{
			BUF[len] = 0;
			ReadFile(file, BUF, len, &tmp, NULL);
			it->anim = ani->GetAnimation(BUF);
		}
		else
			it->anim = NULL;
	}

	need_update = true;
}

//=================================================================================================
void AnimeshInstance::Write(BitStream& s) const
{
	int fai = 0;
	if(frame_end_info)
		fai |= 0x01;
	if(frame_end_info2)
		fai |= 0x02;
	s.WriteCasted<byte>(fai);

	for(vector<Group>::const_iterator it = groups.begin(), end = groups.end(); it != end; ++it)
	{
		s.Write(it->time);
		s.Write(it->speed);
		s.WriteCasted<byte>(it->state);
		s.WriteCasted<byte>(it->prio);
		s.WriteCasted<byte>(it->used_group);
		if(it->anim)
			WriteString1(s, it->anim->name);
		else
			s.WriteCasted<byte>(0);
	}
}

//=================================================================================================
bool AnimeshInstance::Read(BitStream& s)
{
	int fai;

	if(!s.ReadCasted<byte>(fai))
		return false;

	frame_end_info = IS_SET(fai, 0x01);
	frame_end_info2 = IS_SET(fai, 0x02);

	for(vector<Group>::iterator it = groups.begin(), end = groups.end(); it != end; ++it)
	{
		if(s.Read(it->time) &&
			s.Read(it->speed) &&
			s.ReadCasted<byte>(it->state) &&
			s.ReadCasted<byte>(it->prio) &&
			s.ReadCasted<byte>(it->used_group) &&
			ReadString1(s))
		{
			if(BUF[0])
				it->anim = ani->GetAnimation(BUF);
			else
				it->anim = NULL;
		}
		else
			return false;
	}

	need_update = true;
	return true;
}
