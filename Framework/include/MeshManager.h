#pragma once

#include <vector>
#include "Resource.h"

// Meshletの管理クラス。現時点では非Meshletは管理してない。
class MeshManager
{
public:
	MeshManager() {}
	~MeshManager();

	// 現時点ではシーンからの動的追加削除がないのでRegister/Unregisterは用意していない
	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		class DescriptorPool* pPoolCpuVisible,
		const std::vector<struct ResMesh>& resMeshes,
		const std::vector<struct ResMaterial>& resMaterials,
		const class Texture& dummyTexture
	);

	void Term();
	void ClearDrawMeshletBBs(ID3D12GraphicsCommandList6* pCmdList) const;
	void DoCulling(ID3D12GraphicsCommandList6* pCmdList) const;

	const Resource& GetDrawMeshletIndirectArgBB() const;
	const Resource& GetDrawMeshletIndicesBB() const;
	const Resource& GetMeshletMeshMaterialTableSB() const;
	const Resource& GetMeshesDescHeapIndicesCB() const;

	uint32_t GetMeshletCount() const;

private:
	class DescriptorPool* m_pPoolGpuVisible;
	class DescriptorPool* m_pPoolCpuVisible;

	// 要素数は登録されたMesh数
	std::vector<Resource> m_CBs;
	std::vector<Resource> m_VBs;
	std::vector<Resource> m_MeshletsSBs;
	std::vector<Resource> m_MeshletsVerticesSBs;
	std::vector<Resource> m_MeshletsTrianglesSBs;
	std::vector<Resource> m_MeshletsAABBInfosSBs;

	Resource m_MeshletMeshMaterialTableSB;

	Resource m_UnitCubeVB;
	Resource m_UnitCubeIB;

	ComPtr<ID3D12CommandSignature> m_pDrawByHWRasCmdSig;
	ComPtr<ID3D12CommandSignature> m_pDrawBySWRasCmdSig;
	Resource m_DrawMeshletIndirectArgBB;
	Resource m_DrawMeshletIndicesBB;

	Resource m_MeshesDescHeapIndicesCB;
	Resource m_MaterialsDescHeapIndicesCB;

	uint32_t m_MeshletCount = 0;

	MeshManager(const MeshManager&) = delete;
	void operator=(const MeshManager&) = delete;
};
