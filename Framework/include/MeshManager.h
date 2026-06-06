#pragma once

#include <vector>
#include "ResMesh.h"
#include "Resource.h"
#include "Texture.h"

#include <SimpleMath.h>

// Meshletの管理クラス。現時点では非Meshletは管理してない。
class MeshManager
{
public:
	MeshManager() {}
	~MeshManager();

	void Term();

	// 現時点ではシーンからの動的削除がないのでUnregisterは用意していない
	bool RegisterModel(const std::wstring& filePath, const DirectX::SimpleMath::Matrix& worldMat, bool useMetis);

	bool Update
	(
		ID3D12Device* pDevice,
		ID3D12CommandQueue* pQueue,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		class DescriptorPool* pPoolCpuVisible,
		const class Texture& dummyTexture
	);

	bool SetMovableWorldMatrix(ID3D12Device* pDevice, ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& worldMat);

	const Resource& GetDrawOpaqueMeshletIndirectArgBB() const;
	const Resource& GetDrawOpaqueMeshletIndicesBB() const;
	const Resource& GetDrawMaskedMeshletIndirectArgBB() const;
	const Resource& GetDrawMaskedMeshletIndicesBB() const;
	const Resource& GetMeshletMeshMaterialTableSB() const;
	const Resource& GetMeshesDescHeapIndicesCB() const;
	const Resource& GetMaterialsDescHeapIndicesCB() const;
	const Resource& GetUnitCubeVB() const;
	const Resource& GetUnitCubeIB() const;

	const ComPtr<ID3D12CommandSignature>& GetHWRasCmdSig() const;
	const ComPtr<ID3D12CommandSignature>& GetSWRasCmdSig() const;

	uint32_t GetMeshletCount() const;

private:
	std::vector<ResMesh> m_resMeshes;
	std::vector<ResMaterial> m_resMaterials;
	std::vector<DirectX::SimpleMath::Matrix> m_worldMatrices;

	class DescriptorPool* m_pPoolGpuVisible;
	class DescriptorPool* m_pPoolCpuVisible;

	// 要素数は登録されたMesh数
	std::vector<Resource> m_MeshCBs;
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
	Resource m_DrawOpaqueMeshletIndirectArgBB;
	Resource m_DrawOpaqueMeshletIndicesBB;
	Resource m_DrawMaskedMeshletIndirectArgBB;
	Resource m_DrawMaskedMeshletIndicesBB;

	Resource m_MeshesDescHeapIndicesCB;
	Resource m_MaterialsDescHeapIndicesCB;

	uint32_t m_MeshletCount = 0;

	// 要素数は登録されたMaterial数
	std::vector<Resource> m_MaterialCBs;
	std::vector<Texture> m_BaseColorMaps;
	std::vector<Texture> m_MetallicRoughnessMaps;
	std::vector<Texture> m_NormalMaps;
	std::vector<Texture> m_EmissiveMaps;
	std::vector<Texture> m_AOMaps;

	MeshManager(const MeshManager&) = delete;
	void operator=(const MeshManager&) = delete;
};
