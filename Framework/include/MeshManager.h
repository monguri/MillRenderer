#pragma once

#include <vector>

// Meshletの管理クラス。現時点では非Meshletは管理してない。
class MeshManager
{
public:
	MeshManager() {}

	// 現時点ではシーンからの動的追加削除がないのでRegister/Unregisterは用意していない
	void Init(const std::vector<class Mesh*>& pMeshes, const std::vector<class Material*>& pMaterials);

private:
	std::vector<class Mesh*> m_pMeshes;
	std::vector<class Material*> m_pMaterials;

	MeshManager(const MeshManager&) = delete;
	void operator=(const MeshManager&) = delete;
};
