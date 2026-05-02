#include "MeshManager.h"

bool MeshManager::Init
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	class DescriptorPool* pPoolGpuVisible,
	class DescriptorPool* pPoolCpuVisible,
	const std::vector<class Mesh*>& pMeshes,
	const std::vector<class Material*>& pMaterials,
	size_t cbBufferSize
)
{
	m_pMeshes = pMeshes;
	m_pMaterials = pMaterials;

	return true;
}
