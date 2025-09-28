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

	uint32_t vertexCount = static_cast<uint32_t>(resource.Vertices.size());
	m_IndexCount = static_cast<uint32_t>(resource.Indices.size());

	m_IsMeshlet = isMeshlet;

	if (m_IsMeshlet)
	{
		// TriangleListを前提としている
		assert(m_IndexCount % 3 == 0);
		uint32_t triangleCount = m_IndexCount / 3;
		uint32_t biggerCount = std::max(vertexCount, triangleCount);

		// シェーダ側と合わせる
		static constexpr uint32_t NUM_THREAD_MESHLET = 128;
		m_MeshletCount = (biggerCount + NUM_THREAD_MESHLET - 1) / NUM_THREAD_MESHLET;

		struct MeshletInfo
		{
			uint32_t VertCount;
			uint32_t VertOffset;
			uint32_t TriCount;
			uint32_t TriOffset;
		};

		std::vector<MeshletInfo> meshletInfos;
		meshletInfos.resize(m_MeshletCount);

		uint32_t vertexCountQuotient = vertexCount / NUM_THREAD_MESHLET;
		uint32_t triangleCountQuotient = triangleCount / NUM_THREAD_MESHLET;
		for (uint32_t i = 0; i < m_MeshletCount; i++)
		{
			if (i < vertexCountQuotient)
			{
				meshletInfos[i].VertCount = NUM_THREAD_MESHLET;
				meshletInfos[i].VertOffset = i * NUM_THREAD_MESHLET;
			}
			else if (i == vertexCountQuotient)
			{
				meshletInfos[i].VertCount = (vertexCount % NUM_THREAD_MESHLET);
				meshletInfos[i].VertOffset = i * NUM_THREAD_MESHLET;
			}
			else // (i > vertexCountQuotient)
			{
				meshletInfos[i].VertCount = 0;
				meshletInfos[i].VertOffset = vertexCount;
			}

			if (i < triangleCountQuotient)
			{
				meshletInfos[i].TriCount = NUM_THREAD_MESHLET;
				meshletInfos[i].TriOffset = i * NUM_THREAD_MESHLET;
			}
			else if (i == triangleCountQuotient)
			{
				meshletInfos[i].TriCount = (triangleCount % NUM_THREAD_MESHLET);
				meshletInfos[i].TriOffset = i * NUM_THREAD_MESHLET;
			}
			else // (i > triangleCountQuotient)
			{
				meshletInfos[i].TriCount = 0;
				meshletInfos[i].TriOffset = triangleCount;
			}
		}

		if (!m_MeshletInfoSB.InitAsStructuredBuffer<MeshletInfo>(
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

		if (!m_MeshletInfoSB.UploadBufferTypeData<MeshletInfo>(
			pDevice,
			pCmdList,
			m_MeshletCount,
			meshletInfos.data()
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

		if (!m_IB.InitAsStructuredBuffer<uint32_t>(
			pDevice,
			m_IndexCount,
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
		pCmdList->DispatchMesh(m_MeshletCount, 1, 1);
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

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletInfoSBHandle() const
{
	return m_MeshletInfoSB.GetHandleSRV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletVeticesSBHandle() const
{
	return m_VB.GetHandleSRV()->HandleGPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Mesh::GetMesletIndicesSBHandle() const
{
	return m_IB.GetHandleSRV()->HandleGPU;
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

