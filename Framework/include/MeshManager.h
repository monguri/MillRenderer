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
	template<typename MeshCBType>
	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		class DescriptorPool* pPoolCpuVisible,
		const std::vector<struct ResMesh>& resMeshes,
		const std::vector<struct ResMaterial>& resMaterials
	)
	{
		return Init
		(
			pDevice,
			pCmdList,
			pPoolGpuVisible,
			pPoolCpuVisible,
			resMeshes,
			resMaterials,
			sizeof(MeshCBType)
		);
	}

	void Term();

private:
	std::vector<class Material*> m_pMaterials;

	class DescriptorPool* m_pPoolGpuVisible;
	class DescriptorPool* m_pPoolCpuVisible;

	// 登録されたMesh数
	std::vector<Resource> m_VBs;
	std::vector<Resource> m_IBs;
	// 登録されたMesh数 * App::FRAME_COUNT
	std::vector<Resource> m_CBs;
	// 登録されたMesh数
	std::vector<Resource> m_MeshletsSBs;
	std::vector<Resource> m_MeshletsVerticesSBs;
	std::vector<Resource> m_MeshletsTrianglesSBs;
	std::vector<Resource> m_MeshletsAABBInfosSBs;

	Resource m_UnitCubeVB;
	Resource m_UnitCubeIB;

	Resource m_MeshletMeshMaterialTableSB;

	ComPtr<ID3D12CommandSignature> m_pDrawByHWRasCmdSig;
	ComPtr<ID3D12CommandSignature> m_pDrawBySWRasCmdSig;
	Resource m_DrawMeshletIndirectArgBB;
	Resource m_DrawMeshletIndicesBB;

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		class DescriptorPool* pPoolCpuVisible,
		const std::vector<struct ResMesh>& resMeshes,
		const std::vector<struct ResMaterial>& resMaterials,
		size_t cbBufferSize
	);

	MeshManager(const MeshManager&) = delete;
	void operator=(const MeshManager&) = delete;
};
