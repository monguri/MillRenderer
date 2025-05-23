#include "Mesh.h"
#include "Logger.h"
#include "DescriptorPool.h"

Mesh::Mesh()
: m_MaterialId(UINT32_MAX)
, m_IndexCount(0)
, m_Mobility(Mobility::Static)
, m_pPool(nullptr)
{
}

Mesh::~Mesh()
{
	Term();
}

bool Mesh::Init
(
	ID3D12Device* pDevice,
	class DescriptorPool* pPool,
	const ResMesh& resource,
	size_t cbBufferSize
)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	assert(cbBufferSize > 0);

	if (!m_VB.Init<MeshVertex>(pDevice, resource.Vertices.size(), resource.Vertices.data()))
	{
		return false;
	}

	if (!m_IB.Init(pDevice, resource.Indices.size(), resource.Indices.data()))
	{
		return false;
	}

	m_pPool = pPool;
	m_pPool->AddRef();

	for (uint32_t i = 0; i < App::FRAME_COUNT; i++)
	{
		if (!m_CB[i].Init(pDevice, pPool, cbBufferSize))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}
	}

	m_MaterialId = resource.MaterialId;
	m_IndexCount = uint32_t(resource.Indices.size());

	return true;
}

void Mesh::Term()
{
	m_VB.Term();
	m_IB.Term();

	for (uint32_t i = 0; i < App::FRAME_COUNT; i++)
	{
		m_CB[i].Term();
	}

	m_MaterialId = UINT32_MAX;
	m_IndexCount = 0;

	if (m_pPool != nullptr)
	{
		m_pPool->Release();
		m_pPool = nullptr;
	}
}

void Mesh::Draw(ID3D12GraphicsCommandList* pCmdList) const
{
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_VB.GetView();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_IB.GetView();
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);
	pCmdList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}

void* Mesh::GetBufferPtr(uint32_t frameIndex) const
{
	return m_CB[frameIndex].GetPtr();
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetConstantBufferHandle(uint32_t frameIndex) const
{
	return m_CB[frameIndex].GetHandleGPU();
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

