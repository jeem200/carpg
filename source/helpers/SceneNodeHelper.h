#pragma once

//-----------------------------------------------------------------------------
namespace SceneNodeHelper
{
	bool Create(SceneNode*& node, Mesh* mesh);
	void Save(SceneNode* node, StreamWriter& f);
	SceneNode* Load(StreamReader& f);
	// pre V_DEV
	namespace old
	{
		SceneNode* Load(GameReader& f, int version, uint groups = 1u);
		SceneNode* LoadSimple(GameReader& f);
	}
}