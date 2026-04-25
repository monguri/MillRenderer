#pragma once

// Meshletの管理クラス。現時点では非Meshletは管理してない。
class MeshManager
{
	void RegisterMeshlet(const struct ResMesh& resMesh);
	// 現時点ではシーンからのMeshlet削除がないのでUnregisterMeshletは用意していない
};
