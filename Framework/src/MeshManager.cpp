#include "MeshManager.h"

void MeshManager::Init(const std::vector<class Mesh*>& pMeshes, const std::vector<class Material*>& pMaterials)
{
	m_pMeshes = pMeshes;
	m_pMaterials = pMaterials;
}
