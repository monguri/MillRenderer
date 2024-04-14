// DirectXTKにModel.hがあり、名前が重複するとエラーになるのでしょうがなくRenderModel.h/cppにしている
#include "RenderModel.h"
#include "Mesh.h"
#include "Material.h"
#include "InlineUtil.h"

Model::Model()
{
}

Model::~Model()
{
}

void Model::Term()
{
	for (size_t i = 0; i < m_pMeshes.size(); i++)
	{
		SafeTerm(m_pMeshes[i]);
	}
	m_pMeshes.clear();
	m_pMeshes.shrink_to_fit();

	for (size_t i = 0; i < m_pMaterials.size(); i++)
	{
		SafeTerm(m_pMaterials[i]);
	}
}

size_t Model::GetMeshCount() const
{
	return m_pMeshes.size();
}

Mesh* Model::GetMesh(size_t meshIdx) const
{
	if (meshIdx < m_pMeshes.size())
	{
		return m_pMeshes[meshIdx];
	}
	else
	{
		return nullptr;
	}
}

Material* Model::GetMaterial(size_t matIdx) const
{
	if (matIdx < m_pMaterials.size())
	{
		return m_pMaterials[matIdx];
	}
	else
	{
		return nullptr;
	}
}


void Model::SetMeshes(const std::vector<Mesh*>& meshes)
{
	m_pMeshes = meshes;
}

void Model::SetMaterials(const std::vector<Material*>& materials)
{
	m_pMaterials = materials;
}
