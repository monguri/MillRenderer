#include "Mesh.h"
#include "Logger.h"
#include "DescriptorPool.h"

Mesh::Mesh()
: m_MaterialId(UINT32_MAX)
, m_MeshletCount(0)
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
	ID3D12GraphicsCommandList* pCmdList,
	class DescriptorPool* pPool,
	const ResMesh& resource,
	size_t cbBufferSize,
	bool isMeshlet
)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	assert(cbBufferSize > 0);

	size_t vertexCount = resource.Vertices.size();
	m_IndexCount = resource.Indices.size();

	m_IsMeshlet = isMeshlet;

	if (m_IsMeshlet)
	{
		m_MeshletCount = resource.Meshlets.size();

		if (!m_MeshletsSB.InitAsStructuredBuffer<meshopt_Meshlet>(
			pDevice,
			m_MeshletCount,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_MeshletsSB.UploadBufferTypeData<meshopt_Meshlet>(
			pDevice,
			pCmdList,
			m_MeshletCount,
			resource.Meshlets.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		if (!m_MeshletsVerticesSB.InitAsStructuredBuffer<uint32_t>(
			pDevice,
			resource.MeshletsVertices.size(),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_MeshletsVerticesSB.UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			resource.MeshletsVertices.size(),
			resource.MeshletsVertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		if (!m_MeshletsTrianglesBB.InitAsByteAddressBuffer(
			pDevice,
			resource.MeshletsTriangles.size(),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_MeshletsTrianglesBB.UploadBufferTypeData<uint8_t>(
			pDevice,
			pCmdList,
			resource.MeshletsTriangles.size(),
			resource.MeshletsTriangles.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		if (!m_VB.InitAsStructuredBuffer<MeshVertex>(
			pDevice,
			vertexCount,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}
	}
	else
	{
		if (!m_VB.InitAsVertexBuffer<MeshVertex>(
			pDevice,
			vertexCount
		))
		{
			ELOG("Error : Resource::InitAsVertexBuffer() Failed.");
			return false;
		}

		if (!m_IB.InitAsIndexBuffer<uint32_t>(
			pDevice,
			DXGI_FORMAT_R32_UINT,
			m_IndexCount
		))
		{
			ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
			return false;
		}

		if (!m_IB.UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			m_IndexCount,
			resource.Indices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}
	}

	if (!m_VB.UploadBufferTypeData<MeshVertex>(
		pDevice,
		pCmdList,
		vertexCount,
		resource.Vertices.data()
	))
	{
		ELOG("Error : Resource::UploadBufferTypeData() Failed.");
		return false;
	}

	m_pPool = pPool;
	m_pPool->AddRef();

	for (uint32_t i = 0; i < App::FRAME_COUNT; i++)
	{
		if (!m_CB[i].InitAsConstantBuffer(
			pDevice,
			cbBufferSize,
			D3D12_HEAP_TYPE_UPLOAD,
			pPool
		))
		{
			ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
			return false;
		}
	}

	m_MaterialId = resource.MaterialId;

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

	m_MeshletsSB.Term();
	m_MeshletsVerticesSB.Term();
	m_MeshletsTrianglesBB.Term();

	m_MaterialId = UINT32_MAX;
	m_IndexCount = 0;

	if (m_pPool != nullptr)
	{
		m_pPool->Release();
		m_pPool = nullptr;
	}
}

void Mesh::Draw(ID3D12GraphicsCommandList6* pCmdList) const
{
	if (m_IsMeshlet)
	{
		pCmdList->DispatchMesh(static_cast<UINT>(m_MeshletCount), 1, 1);
	}
	else
	{
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_VB.GetVBV();
		const D3D12_INDEX_BUFFER_VIEW& IBV = m_IB.GetIBV();

		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->IASetVertexBuffers(0, 1, &VBV);
		pCmdList->IASetIndexBuffer(&IBV);
		pCmdList->DrawIndexedInstanced(static_cast<UINT>(m_IndexCount), 1, 0, 0, 0);
	}
}

void Mesh::UnmapConstantBuffer(uint32_t frameIndex) const
{
	m_CB[frameIndex].Unmap();
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetConstantBufferHandle(uint32_t frameIndex) const
{
	return m_CB[frameIndex].GetHandleCBV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetVertexBufferSBHandle() const
{
	return m_VB.GetHandleSRV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletsSBHandle() const
{
	return m_MeshletsSB.GetHandleSRV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletsVerticesSBHandle() const
{
	return m_MeshletsVerticesSB.GetHandleSRV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletsTrianglesBBHandle() const
{
	return m_MeshletsTrianglesBB.GetHandleSRV()->HandleGPU;
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

