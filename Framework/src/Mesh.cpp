#include "Mesh.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include <DirectXMath.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

Mesh::Mesh()
: m_MaterialId(UINT32_MAX)
, m_MeshletCount(0)
, m_IndexCount(0)
, m_SphereIndexCount(0)
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
			nullptr,
			L"MeshletsSB"
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
			nullptr,
			L"MeshletsVerticesSB"
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

		// TODO: uint8_tの3つをuint32_tに詰め込んでBBで扱いたい。
		// 無駄にVRAMとメモリ帯域を使っている。Pixでの値確認はしやすいが。
		std::vector<uint32_t> meshletsTriangles;
		for (uint8_t index : resource.MeshletsTriangles)
		{
			meshletsTriangles.push_back(static_cast<uint32_t>(index));
		}

		if (!m_MeshletsTrianglesBB.InitAsStructuredBuffer<uint32_t>(
			pDevice,
			meshletsTriangles.size(),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr,
			L"MeshletsTrianglesBB"
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_MeshletsTrianglesBB.UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			meshletsTriangles.size(),
			meshletsTriangles.data()
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
			nullptr,
			L"SbVertexBuffer"
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
			nullptr,
			L"SbIndexBuffer"
		))
		{
			ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
			return false;
		}

		assert(resource.Bounds.size() == m_MeshletCount);
#if 0
		m_BoundingSphereVBs.resize(m_MeshletCount);
		m_BoundingSphereIBs.resize(m_MeshletCount);

		// CreateBoundingSphere()で使う一時変数だが毎回確保と解放をしないようにスコープを上げておく
		std::vector<DirectX::XMFLOAT3> boundingSphereVertices;
		std::vector<uint32_t> boundingSphereIndices;

		for (uint32_t i = 0; i < m_MeshletCount; i++)
		{
			//TODO: とりあえず一個ずつcenterとradiusをずらしたものを作る
			// 本来は一個作ってインスタンス描画したい
			CreateBoundingSphere(resource.Bounds[i], boundingSphereVertices, boundingSphereIndices);

			if (!m_BoundingSphereVBs[i].InitAsVertexBuffer<DirectX::XMFLOAT3>(
				pDevice,
				boundingSphereVertices.size()
			))
			{
				ELOG("Error : Resource::InitAsVertexBuffer() Failed.");
				return false;
			}

			if (!m_BoundingSphereIBs[i].InitAsIndexBuffer<uint32_t>(
				pDevice,
				DXGI_FORMAT_R32_UINT,
				boundingSphereIndices.size()
			))
			{
				ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
				return false;
			}

			if (!m_BoundingSphereVBs[i].UploadBufferTypeData<DirectX::XMFLOAT3>(
				pDevice,
				pCmdList,
				boundingSphereVertices.size(),
				boundingSphereVertices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			if (!m_BoundingSphereIBs[i].UploadBufferTypeData<uint32_t>(
				pDevice,
				pCmdList,
				boundingSphereIndices.size(),
				boundingSphereIndices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}
		}
#else
		std::vector<DirectX::XMFLOAT3> boundingSphereVertices;
		std::vector<uint32_t> boundingSphereIndices;
		const uint32_t SPHERE_SEGMENT_COUNT = 4;
		CreateSphere(SPHERE_SEGMENT_COUNT, boundingSphereVertices, boundingSphereIndices);

		m_SphereIndexCount = boundingSphereIndices.size();

		if (!m_SphereVB.InitAsVertexBuffer<DirectX::XMFLOAT3>(
			pDevice,
			boundingSphereVertices.size()
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_SphereIB.InitAsIndexBuffer<uint32_t>(
			pDevice,
			DXGI_FORMAT_R32_UINT,
			boundingSphereIndices.size()
		))
		{
			ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
			return false;
		}

		if (!m_SphereVB.UploadBufferTypeData<DirectX::XMFLOAT3>(
			pDevice,
			pCmdList,
			boundingSphereVertices.size(),
			boundingSphereVertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		if (!m_SphereIB.UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			boundingSphereIndices.size(),
			boundingSphereIndices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		struct SphereInfo
		{
			Vector3 center;
			float radius;
		};

		std::vector<SphereInfo> infos(m_MeshletCount);

		for (uint32_t i = 0; i < m_MeshletCount; i++)
		{
			infos[i].center = Vector3(resource.Bounds[i].center);
			infos[i].radius = resource.Bounds[i].radius;
		}

		if (!m_BoundingSphereInfosSB.InitAsStructuredBuffer<SphereInfo>(
			pDevice,
			m_MeshletCount,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPool,
			nullptr,
			L"BoundingSphereInfoSB"
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_BoundingSphereInfosSB.UploadBufferTypeData<SphereInfo>(
			pDevice,
			pCmdList,
			infos.size(),
			infos.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}
#endif
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
			pPool,
			L"CbMesh"
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

	for (uint32_t i = 0; i < m_MeshletCount; i++)
	{
		m_BoundingSphereVBs[i].Term();
		m_BoundingSphereIBs[i].Term();
	}

	m_SphereVB.Term();
	m_SphereIB.Term();
	m_BoundingSphereInfosSB.Term();

	m_MaterialId = UINT32_MAX;
	m_IndexCount = 0;
	m_SphereIndexCount = 0;

	if (m_pPool != nullptr)
	{
		m_pPool->Release();
		m_pPool = nullptr;
	}
}

void Mesh::CreateSphere(uint32_t segmentCount, std::vector<struct DirectX::XMFLOAT3>& outVertices, std::vector<uint32_t>& outIndices)
{
	using namespace DirectX;

    outVertices.clear();
    outIndices.clear();

    const uint32_t verticalSegments = segmentCount;
    const uint32_t horizontalSegments = segmentCount * 2;

    // Create rings of outVertices at progressively higher latitudes.
	outVertices.reserve((verticalSegments + 1) * (horizontalSegments + 1));
    for (uint32_t i = 0; i <= verticalSegments; i++)
    {
        const float latitude = (float(i) * XM_PI / float(verticalSegments)) - XM_PIDIV2;
        float dy, dxz;

        XMScalarSinCos(&dy, &dxz, latitude);

        // Create a single ring of outVertices at this latitude.
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const float longitude = float(j) * XM_2PI / float(horizontalSegments);
            float dx, dz;

            XMScalarSinCos(&dx, &dz, longitude);

            dx *= dxz;
            dz *= dxz;

            outVertices.emplace_back(dx, dy, dz);
        }
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    const uint32_t stride = horizontalSegments + 1;

	outIndices.reserve(verticalSegments * horizontalSegments * 6);
    for (uint32_t i = 0; i < verticalSegments; i++)
    {
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const uint32_t nextI = i + 1;
            const uint32_t nextJ = (j + 1) % stride;

            outIndices.emplace_back(i * stride + j);
            outIndices.emplace_back(nextI * stride + j);
            outIndices.emplace_back(i * stride + nextJ);

            outIndices.emplace_back(i * stride + nextJ);
            outIndices.emplace_back(nextI * stride + j);
            outIndices.emplace_back(nextI * stride + nextJ);
        }
    }
}

//TODO: とりあえず一個ずつcenterとradiusをずらしたものを作る
// DirectXTK12、Geometry.cpp/hのDirectX::ComputeSphereを参考にしている
void Mesh::CreateBoundingSphere(const meshopt_Bounds& meshletBounds, std::vector<DirectX::XMFLOAT3>& outVertices, std::vector<uint32_t>& outIndices)
{
	using namespace DirectX;

    outVertices.clear();
    outIndices.clear();

    const uint32_t verticalSegments = 4;
    const uint32_t horizontalSegments = 4 * 2;

    // Create rings of outVertices at progressively higher latitudes.
	outVertices.reserve((verticalSegments + 1) * (horizontalSegments + 1));
    for (uint32_t i = 0; i <= verticalSegments; i++)
    {
        const float latitude = (float(i) * XM_PI / float(verticalSegments)) - XM_PIDIV2;
        float dy, dxz;

        XMScalarSinCos(&dy, &dxz, latitude);

		dy *= meshletBounds.radius;
		dy += meshletBounds.center[1];

		dxz *= meshletBounds.radius;

        // Create a single ring of outVertices at this latitude.
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const float longitude = float(j) * XM_2PI / float(horizontalSegments);
            float dx, dz;

            XMScalarSinCos(&dx, &dz, longitude);

            dx *= dxz;
            dz *= dxz;

			dx += meshletBounds.center[0];
			dz += meshletBounds.center[2];

            outVertices.emplace_back(dx, dy, dz);
        }
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    const uint32_t stride = horizontalSegments + 1;

	outIndices.reserve(verticalSegments * horizontalSegments * 6);
    for (uint32_t i = 0; i < verticalSegments; i++)
    {
        for (uint32_t j = 0; j <= horizontalSegments; j++)
        {
            const uint32_t nextI = i + 1;
            const uint32_t nextJ = (j + 1) % stride;

            outIndices.emplace_back(i * stride + j);
            outIndices.emplace_back(nextI * stride + j);
            outIndices.emplace_back(i * stride + nextJ);

            outIndices.emplace_back(i * stride + nextJ);
            outIndices.emplace_back(nextI * stride + j);
            outIndices.emplace_back(nextI * stride + nextJ);
        }
    }
}

void Mesh::DrawByHWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const
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

void Mesh::DrawBySWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet);
	pCmdList->Dispatch(static_cast<UINT>(m_MeshletCount), 1, 1);
}

void Mesh::DrawMeshletBoundingSphere(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet);

#if 0
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (uint32_t i = 0; i < m_MeshletCount; i++)
	{
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_BoundingSphereVBs[i].GetVBV();
		const D3D12_INDEX_BUFFER_VIEW& IBV = m_BoundingSphereIBs[i].GetIBV();

		pCmdList->IASetVertexBuffers(0, 1, &VBV);
		pCmdList->IASetIndexBuffer(&IBV);

		// TODO: とりあえず
		const uint32_t verticalSegments = 4;
		const uint32_t horizontalSegments = 4 * 2;
		pCmdList->DrawIndexedInstanced(verticalSegments * horizontalSegments * 6, 1, 0, 0, 0);
	}
#else
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_SphereVB.GetVBV();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_SphereIB.GetIBV();

	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);

	pCmdList->DrawIndexedInstanced(static_cast<UINT>(m_SphereIndexCount), static_cast<UINT>(m_MeshletCount), 0, 0, 0);
#endif
}

void Mesh::UnmapConstantBuffer(uint32_t frameIndex) const
{
	m_CB[frameIndex].Unmap();
}

const DescriptorHandle& Mesh::GetConstantBufferHandle(uint32_t frameIndex) const
{
	return *m_CB[frameIndex].GetHandleCBV();
}

const DescriptorHandle& Mesh::GetVertexBufferSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_VB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetIndexBufferSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_IB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMesletsSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMesletsVerticesSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsVerticesSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMesletsTrianglesBBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsTrianglesBB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMeshletsBoundingSphereInfosSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_BoundingSphereInfosSB.GetHandleSRV();
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

const D3D12_INPUT_ELEMENT_DESC Mesh::WireframeInputElements[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
};

const D3D12_INPUT_LAYOUT_DESC Mesh::WireframeInputLayout = {
	Mesh::WireframeInputElements,
	Mesh::WireframeInputElementCount
};
