#include "Mesh.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include <DirectXHelpers.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr uint32_t MAX_DRAW_MESHLET_COUNT = 1024;

	// DirectXTK12、Geometry.cpp/hのDirectX::ComputeSphereを参考にしている
	void CreateUnitSphereMesh(uint32_t segmentCount, std::vector<struct Vector3>& outVertices, std::vector<uint32_t>& outIndices)
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

	// DirectXTK12、Geometry.cpp/hのDirectX::ComputeBoxを参考にしている
	void CreateUnitCubeMesh(std::vector<struct Vector3>& outVertices, std::vector<uint32_t>& outIndices)
	{
		using namespace DirectX;

		outVertices.clear();
		outIndices.clear();

		// A box has six faces, each one pointing in a different direction.
		constexpr uint32_t FACE_COUNT = 6;

		static const XMVECTORF32 FACE_NORMALS[FACE_COUNT] =
		{
			{ { {  0,  0,  1, 0 } } },
			{ { {  0,  0, -1, 0 } } },
			{ { {  1,  0,  0, 0 } } },
			{ { { -1,  0,  0, 0 } } },
			{ { {  0,  1,  0, 0 } } },
			{ { {  0, -1,  0, 0 } } },
		};

		outVertices.reserve(FACE_COUNT * 4);
		outIndices.reserve(FACE_COUNT * 6);

		// Create each face in turn.
		for (uint32_t i = 0; i < FACE_COUNT; i++)
		{
			const XMVECTOR normal = FACE_NORMALS[i];

			// Get two vectors perpendicular both to the face normal and to each other.
			const XMVECTORF32 basis = (i >= 4) ? XMVECTORF32{{{0.0f, 0.0f, 1.0f, 0.0f}}} : XMVECTORF32{{{0.0f, 1.0f, 0.0f, 0.0f}}};

			const XMVECTOR side1 = XMVector3Cross(normal, basis);
			const XMVECTOR side2 = XMVector3Cross(normal, side1);

			// Six indices (two triangles) per face.
			const uint32_t vbase = static_cast<uint32_t>(outVertices.size());
			outIndices.emplace_back(vbase + 0);
			outIndices.emplace_back(vbase + 1);
			outIndices.emplace_back(vbase + 2);

			outIndices.emplace_back(vbase + 0);
			outIndices.emplace_back(vbase + 2);
			outIndices.emplace_back(vbase + 3);

			// Four vertices per face.
			// (normal - side1 - side2)
			const XMVECTOR& v0 = XMVectorSubtract(XMVectorSubtract(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v0), XMVectorGetY(v0), XMVectorGetZ(v0));

			// (normal - side1 + side2)
			const XMVECTOR& v1 = XMVectorAdd(XMVectorSubtract(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v1), XMVectorGetY(v1), XMVectorGetZ(v1));

			// (normal + side1 + side2)
			const XMVECTOR& v2 = XMVectorAdd(XMVectorAdd(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v2), XMVectorGetY(v2), XMVectorGetZ(v2));

			// (normal + side1 - side2)
			const XMVECTOR& v3 = XMVectorSubtract(XMVectorAdd(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v3), XMVectorGetY(v3), XMVectorGetZ(v3));
		}
	}
}

Mesh::Mesh()
: m_MaterialId(UINT32_MAX)
, m_MeshletCount(0)
, m_IndexCount(0)
, m_SphereIndexCount(0)
, m_Mobility(Mobility::Static)
, m_pPoolGpuVisible(nullptr)
, m_pPoolCpuVisible(nullptr)
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
	class DescriptorPool* pPoolCpuVisible,
	const ResMesh& resource,
	size_t cbBufferSize,
	bool isMeshlet,
	bool useMeshManager
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
	m_UseMeshManager = useMeshManager;

	if (m_IsMeshlet)
	{
		m_MeshletCount = resource.Meshlets.size();

		if (!m_MeshletsSB.InitAsStructuredBuffer<meshopt_Meshlet>(
			pDevice,
			m_MeshletCount,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPoolGpuVisible,
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
			pPoolGpuVisible,
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
			pPoolGpuVisible,
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
			pPoolGpuVisible,
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
			pPoolGpuVisible,
			nullptr,
			L"SbIndexBuffer"
		))
		{
			ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
			return false;
		}

		if (!m_UseMeshManager)
		{
#if 0
			//TODO: BoundingSphere関連はすべて現在未使用でデッドコードになっている。World行列に不均一スケールがあると楕円球になり扱いにくいため
			assert(resource.Bounds.size() == m_MeshletCount);
			std::vector<Vector3> boundingSphereVertices;
			std::vector<uint32_t> boundingSphereIndices;
			const uint32_t SPHERE_SEGMENT_COUNT = 4;
			CreateUnitSphereMesh(SPHERE_SEGMENT_COUNT, boundingSphereVertices, boundingSphereIndices);

			m_SphereIndexCount = boundingSphereIndices.size();

			if (!m_UnitSphereVB.InitAsVertexBuffer<Vector3>(
				pDevice,
				boundingSphereVertices.size()
			))
			{
				ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
				return false;
			}

			if (!m_UnitSphereIB.InitAsIndexBuffer<uint32_t>(
				pDevice,
				DXGI_FORMAT_R32_UINT,
				boundingSphereIndices.size()
			))
			{
				ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
				return false;
			}

			if (!m_UnitSphereVB.UploadBufferTypeData<Vector3>(
				pDevice,
				pCmdList,
				boundingSphereVertices.size(),
				boundingSphereVertices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			if (!m_UnitSphereIB.UploadBufferTypeData<uint32_t>(
				pDevice,
				pCmdList,
				boundingSphereIndices.size(),
				boundingSphereIndices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			std::vector<AABB> sphereInfos(m_MeshletCount);

			for (uint32_t i = 0; i < m_MeshletCount; i++)
			{
				sphereInfos[i].Center = Vector3(resource.Bounds[i].center);
				sphereInfos[i].HalfExtent = Vector3(resource.Bounds[i].radius);
			}

			if (!m_BoundingSphereInfosSB.InitAsStructuredBuffer<AABB>(
				pDevice,
				m_MeshletCount,
				D3D12_RESOURCE_FLAG_NONE,
				D3D12_RESOURCE_STATE_COMMON,
				pPoolGpuVisible,
				nullptr,
				L"BoundingSpheresphereInfosB"
			))
			{
				ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
				return false;
			}

			if (!m_BoundingSphereInfosSB.UploadBufferTypeData<AABB>(
				pDevice,
				pCmdList,
				sphereInfos.size(),
				sphereInfos.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}
#endif

			assert(resource.AABBs.size() == m_MeshletCount);
			std::vector<Vector3> cubeVertices;
			std::vector<uint32_t> cubeIndices;
			CreateUnitCubeMesh(cubeVertices, cubeIndices);

			if (!m_UnitCubeVB.InitAsVertexBuffer<Vector3>(
				pDevice,
				cubeVertices.size()
			))
			{
				ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
				return false;
			}

			if (!m_UnitCubeIB.InitAsIndexBuffer<uint32_t>(
				pDevice,
				DXGI_FORMAT_R32_UINT,
				cubeIndices.size()
			))
			{
				ELOG("Error : Resource::InitAsIndexBuffer() Failed.");
				return false;
			}

			if (!m_UnitCubeVB.UploadBufferTypeData<Vector3>(
				pDevice,
				pCmdList,
				cubeVertices.size(),
				cubeVertices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			if (!m_UnitCubeIB.UploadBufferTypeData<uint32_t>(
				pDevice,
				pCmdList,
				cubeIndices.size(),
				cubeIndices.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			if (!m_AABBInfosSB.InitAsStructuredBuffer<AABB>(
				pDevice,
				m_MeshletCount,
				D3D12_RESOURCE_FLAG_NONE,
				D3D12_RESOURCE_STATE_COMMON,
				pPoolGpuVisible,
				nullptr,
				L"AABBInfosSB"
			))
			{
				ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
				return false;
			}

			if (!m_AABBInfosSB.UploadBufferTypeData<AABB>(
				pDevice,
				pCmdList,
				resource.AABBs.size(),
				resource.AABBs.data()
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			// MeshletのHWRasterizer描画用のCommandSignatureの生成
			{
				D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
				argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

				D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
				cmdSigDesc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
				cmdSigDesc.NumArgumentDescs = 1;
				cmdSigDesc.pArgumentDescs = &argDesc;

				HRESULT hr = pDevice->CreateCommandSignature(&cmdSigDesc, nullptr, IID_PPV_ARGS(&m_pDrawByHWRasCmdSig));
				if (FAILED(hr))
				{
					ELOG("Error : ID3D12Device::CreateCommandSignature() Failed.");
					return false;
				}
			}

			// MeshletのSWRasterizer描画用のCommandSignatureの生成
			{
				D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
				argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

				D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
				cmdSigDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
				cmdSigDesc.NumArgumentDescs = 1;
				cmdSigDesc.pArgumentDescs = &argDesc;

				HRESULT hr = pDevice->CreateCommandSignature(&cmdSigDesc, nullptr, IID_PPV_ARGS(&m_pDrawBySWRasCmdSig));
				if (FAILED(hr))
				{
					ELOG("Error : ID3D12Device::CreateCommandSignature() Failed.");
					return false;
				}
			}

			// Meshlet描画用のMeshletカウンターのDispatchIndirectArgの生成
			if (!m_DrawMeshletIndirectArgBB.InitAsByteAddressBuffer
			(
				pDevice,
				3 * sizeof(uint32_t),
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COMMON,
				pPoolGpuVisible,
				pPoolGpuVisible,
				pPoolCpuVisible,
				L"DrawMeshletIndirectArgBB"
			))
			{
				ELOG("Error : Resource::InitAsByteAddressBuffe() Failed.");
				return false;
			}

			DirectX::TransitionResource(pCmdList, m_DrawMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			// Meshlet描画用のカリング済みMeshletIdxリストの生成
			if (!m_DrawMeshletListBB.InitAsByteAddressBuffer
			(
				pDevice,
				MAX_DRAW_MESHLET_COUNT * sizeof(uint32_t),
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COMMON,
				pPoolGpuVisible,
				pPoolGpuVisible,
				pPoolCpuVisible,
				L"DrawMeshletMeshletListBB"
			))
			{
				ELOG("Error : Resource::InitAsByteAddressBuffe() Failed.");
				return false;
			}

			DirectX::TransitionResource(pCmdList, m_DrawMeshletListBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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

	m_pPoolGpuVisible = pPoolGpuVisible;
	m_pPoolGpuVisible->AddRef();
	m_pPoolCpuVisible = pPoolCpuVisible;
	m_pPoolCpuVisible->AddRef();

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

	m_UnitSphereVB.Term();
	m_UnitSphereIB.Term();
	m_BoundingSphereInfosSB.Term();

	m_UnitCubeVB.Term();
	m_UnitCubeIB.Term();
	m_AABBInfosSB.Term();

	m_pDrawByHWRasCmdSig.Reset();
	m_pDrawBySWRasCmdSig.Reset();

	m_DrawMeshletIndirectArgBB.Term();
	m_DrawMeshletListBB.Term();

	m_MaterialId = UINT32_MAX;
	m_IndexCount = 0;
	m_SphereIndexCount = 0;

	if (m_pPoolGpuVisible != nullptr)
	{
		m_pPoolGpuVisible->Release();
		m_pPoolGpuVisible = nullptr;
	}

	if (m_pPoolCpuVisible != nullptr)
	{
		m_pPoolCpuVisible->Release();
		m_pPoolCpuVisible = nullptr;
	}
}

void Mesh::ClearDrawMeshletBBs(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet && !m_UseMeshManager);

	uint32_t clearValue[4] = {0, 0, 0, 0};
	// 本来はX=0、Y=1、Z=1にしたいが、ClearUavWithUintValue()とByteAddressBufferではそれができないようだ。[0]の値ですべてクリアされてしまう。よってY=1、Z=1はシェーダで入れる。
	m_DrawMeshletIndirectArgBB.ClearUavWithUintValue(pCmdList, clearValue);
	m_DrawMeshletListBB.ClearUavWithUintValue(pCmdList, clearValue);

	m_DrawMeshletIndirectArgBB.BarrierUAV(pCmdList);
	m_DrawMeshletListBB.BarrierUAV(pCmdList);
}

void Mesh::DoMeshletCulling(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet && !m_UseMeshManager);

	pCmdList->SetComputeRoot32BitConstant(0, static_cast<UINT>(m_MeshletCount), 0);

	// シェーダ側と合わせている
	constexpr size_t GROUP_SIZE_X = 64;
	// グループ数は切り上げ
	UINT NumGroupX = static_cast<UINT>((m_MeshletCount + GROUP_SIZE_X - 1) / GROUP_SIZE_X);
	pCmdList->Dispatch(NumGroupX, 1, 1);

	m_DrawMeshletIndirectArgBB.BarrierUAV(pCmdList);
	m_DrawMeshletListBB.BarrierUAV(pCmdList);
}

void Mesh::DrawByHWRasterizer(ID3D12GraphicsCommandList6* pCmdList, bool useCulling) const
{
	if (m_IsMeshlet && !m_UseMeshManager)
	{
		if (useCulling)
		{
			DirectX::TransitionResource(pCmdList, m_DrawMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			pCmdList->ExecuteIndirect(m_pDrawByHWRasCmdSig.Get(), 1, m_DrawMeshletIndirectArgBB.GetResource(), 0, nullptr, 0);
			DirectX::TransitionResource(pCmdList, m_DrawMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		else
		{
			pCmdList->DispatchMesh(static_cast<UINT>(m_MeshletCount), 1, 1);
		}
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
	assert(m_IsMeshlet && !m_UseMeshManager);

	DirectX::TransitionResource(pCmdList, m_DrawMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

	pCmdList->ExecuteIndirect(m_pDrawBySWRasCmdSig.Get(), 1, m_DrawMeshletIndirectArgBB.GetResource(), 0, nullptr, 0);

	DirectX::TransitionResource(pCmdList, m_DrawMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Mesh::DrawMeshletBoundingSphere(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet && !m_UseMeshManager);

	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_UnitSphereVB.GetVBV();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_UnitSphereIB.GetIBV();

	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);

	pCmdList->DrawIndexedInstanced(static_cast<UINT>(m_SphereIndexCount), static_cast<UINT>(m_MeshletCount), 0, 0, 0);
}

void Mesh::DrawMeshletAABB(ID3D12GraphicsCommandList6* pCmdList) const
{
	assert(m_IsMeshlet && !m_UseMeshManager);

	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_UnitCubeVB.GetVBV();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_UnitCubeIB.GetIBV();

	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);

	// Cubeは6面12トライアングル36インデックス
	pCmdList->DrawIndexedInstanced(36, static_cast<UINT>(m_MeshletCount), 0, 0, 0);
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

const DescriptorHandle& Mesh::GetMeshletsSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMeshletsVerticesSBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsVerticesSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMeshletsTrianglesBBHandle() const
{
	assert(m_IsMeshlet);
	return *m_MeshletsTrianglesBB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMeshletsBoundingSphereInfosSBHandle() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return *m_BoundingSphereInfosSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetMeshletsAABBInfosSBHandle() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return *m_AABBInfosSB.GetHandleSRV();
}

const DescriptorHandle& Mesh::GetDrawMeshletIndirectArgBBHandle() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return *m_DrawMeshletIndirectArgBB.GetHandleUAV();
}

const Resource& Mesh::GetDrawMeshletListBB() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return m_DrawMeshletListBB;
}

const DescriptorHandle& Mesh::GetDrawMeshletListBBUavHandle() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return *m_DrawMeshletListBB.GetHandleUAV();
}

const DescriptorHandle& Mesh::GetDrawMeshletListBBSrvHandle() const
{
	assert(m_IsMeshlet && !m_UseMeshManager);
	return *m_DrawMeshletListBB.GetHandleSRV();
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

const D3D12_INPUT_ELEMENT_DESC Mesh::PosOnlyInputElements[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
};

const D3D12_INPUT_LAYOUT_DESC Mesh::PosOnlyInputLayout = {
	Mesh::PosOnlyInputElements,
	Mesh::PosOnlyInputElementCount
};
