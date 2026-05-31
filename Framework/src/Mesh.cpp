#include "Mesh.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include <DirectXHelpers.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

Mesh::Mesh()
: m_MaterialIdx(UINT32_MAX)
, m_IndexCount(0)
, m_Mobility(Mobility::Static)
, m_pPoolGpuVisible(nullptr)
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
	class DescriptorPool* pPoolGpuVisible,
	const ResMesh& resource,
	size_t cbBufferSize
)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	assert(cbBufferSize > 0);

	size_t vertexCount = resource.Vertices.size();
	m_IndexCount = resource.Indices.size();

	if (!m_VB.InitAsVertexBuffer<MeshVertex>(
		pDevice,
		vertexCount,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		L"VB"
	))
	{
		ELOG("Error : Resource::InitAsVertexBuffer() Failed.");
		return false;
	}

	if (!m_IB.InitAsIndexBuffer<uint32_t>(
		pDevice,
		DXGI_FORMAT_R32_UINT,
		m_IndexCount,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		L"IB"
	))
	{
		ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
		return false;
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

	m_pPoolGpuVisible = pPoolGpuVisible;
	m_pPoolGpuVisible->AddRef();

	for (uint32_t i = 0; i < App::FRAME_COUNT; i++)
	{
		if (!m_CB[i].InitAsConstantBuffer(
			pDevice,
			cbBufferSize,
			D3D12_HEAP_TYPE_UPLOAD,
			pPoolGpuVisible,
			L"CbMesh"
		))
		{
			ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
			return false;
		}
	}

	m_MaterialIdx = resource.MaterialIdx;

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

	m_MaterialIdx = UINT32_MAX;
	m_IndexCount = 0;

	if (m_pPoolGpuVisible != nullptr)
	{
		m_pPoolGpuVisible->Release();
		m_pPoolGpuVisible = nullptr;
	}
}

void Mesh::Draw(ID3D12GraphicsCommandList6* pCmdList) const
{
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_VB.GetVBV();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_IB.GetIBV();

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);
	pCmdList->DrawIndexedInstanced(static_cast<UINT>(m_IndexCount), 1, 0, 0, 0);
}

void Mesh::UnmapConstantBuffer(uint32_t frameIndex) const
{
	m_CB[frameIndex].Unmap();
}

const DescriptorHandle& Mesh::GetConstantBufferHandle(uint32_t frameIndex) const
{
	return *m_CB[frameIndex].GetHandleCBV();
}
uint32_t Mesh::GetMaterialIdx() const
{
	return m_MaterialIdx;
}

Mobility Mesh::GetMobility() const
{
	return m_Mobility;
}

void Mesh::SetMobility(Mobility mobility)
{
	m_Mobility = mobility;
}
