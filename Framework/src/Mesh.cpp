#include "Mesh.h"

Mesh::Mesh()
: m_MaterialId(UINT32_MAX)
, m_IndexCount(0)
, m_Mobility(Mobility::Static)
{
}

Mesh::~Mesh()
{
	Term();
}

bool Mesh::Init(ID3D12Device* pDevice, const ResMesh& resource)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	if (!m_VB.Init<MeshVertex>(pDevice, resource.Vertices.size(), resource.Vertices.data()))
	{
		return false;
	}

	if (!m_IB.Init(pDevice, resource.Indices.size(), resource.Indices.data()))
	{
		return false;
	}

	m_MaterialId = resource.MaterialId;
	m_IndexCount = uint32_t(resource.Indices.size());

	return true;
}

void Mesh::Term()
{
	m_VB.Term();
	m_IB.Term();
	m_MaterialId = UINT32_MAX;
	m_IndexCount = 0;
}

void Mesh::Draw(ID3D12GraphicsCommandList* pCmdList)
{
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_VB.GetView();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_IB.GetView();
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);
	pCmdList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}

uint32_t Mesh::GetMaterialId() const
{
	return m_MaterialId;
}

Mobility Mesh::GetMobility() const
{
	return m_Mobility;
}

void Mesh::SetMobility(Mobility mobility)
{
	m_Mobility = mobility;
}

