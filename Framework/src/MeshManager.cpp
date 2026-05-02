#include "MeshManager.h"
#include "DescriptorPool.h"
#include "ResMesh.h"
#include "Material.h"
#include "Logger.h"
#include "App.h"

#include <DirectXHelpers.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

namespace
{
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

MeshManager::~MeshManager()
{
	Term();
}

bool MeshManager::Init
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	class DescriptorPool* pPoolGpuVisible,
	class DescriptorPool* pPoolCpuVisible,
	const std::vector<struct ResMesh>& resMeshes,
	const std::vector<struct ResMaterial>& resMaterials,
	size_t cbBufferSize
)
{
	assert(pDevice != nullptr);
	assert(pPoolGpuVisible != nullptr);
	assert(pPoolCpuVisible != nullptr);
	assert(cbBufferSize > 0);

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

	size_t meshCount = resMeshes.size();
	m_VBs.resize(meshCount);
	m_IBs.resize(meshCount);
	m_CBs.resize(meshCount * App::FRAME_COUNT);
	m_MeshletsSBs.resize(meshCount);
	m_MeshletsVerticesSBs.resize(meshCount);
	m_MeshletsTrianglesSBs.resize(meshCount);
	m_MeshletsAABBInfosSBs.resize(meshCount);

	size_t totalMeshletCount = 0;

	for (size_t meshIdx = 0; meshIdx < meshCount; meshIdx++)
	{
		const ResMesh& resMesh = resMeshes[meshIdx];
		size_t meshletCount = resMesh.Meshlets.size();

		if (!m_VBs[meshIdx].InitAsStructuredBuffer<MeshVertex>(
			pDevice,
			resMesh.Vertices.size(),
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

		if (!m_IBs[meshIdx].InitAsStructuredBuffer<uint32_t>(
			pDevice,
			resMesh.Indices.size(),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COMMON,
			pPoolGpuVisible,
			nullptr,
			L"SbIndexBuffer"
		))
		{
			ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
			return false;
		}

		if (!m_MeshletsSBs[meshIdx].InitAsStructuredBuffer<meshopt_Meshlet>(
			pDevice,
			meshletCount,
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

		if (!m_MeshletsSBs[meshIdx].UploadBufferTypeData<meshopt_Meshlet>(
			pDevice,
			pCmdList,
			meshletCount,
			resMesh.Meshlets.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		if (!m_MeshletsVerticesSBs[meshIdx].InitAsStructuredBuffer<uint32_t>(
			pDevice,
			resMesh.MeshletsVertices.size(),
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

		if (!m_MeshletsVerticesSBs[meshIdx].UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			resMesh.MeshletsVertices.size(),
			resMesh.MeshletsVertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		// TODO: uint8_tの3つをuint32_tに詰め込んでBBで扱いたい。
		// 無駄にVRAMとメモリ帯域を使っている。Pixでの値確認はしやすいが。
		std::vector<uint32_t> meshletsTriangles;
		for (uint8_t index : resMesh.MeshletsTriangles)
		{
			meshletsTriangles.push_back(static_cast<uint32_t>(index));
		}

		if (!m_MeshletsTrianglesSBs[meshIdx].InitAsStructuredBuffer<uint32_t>(
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

		if (!m_MeshletsTrianglesSBs[meshIdx].UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			meshletsTriangles.size(),
			meshletsTriangles.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		assert(resMesh.AABBs.size() == meshletCount);

		if (!m_MeshletsAABBInfosSBs[meshIdx].InitAsStructuredBuffer<AABB>(
			pDevice,
			meshletCount,
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

		if (!m_MeshletsAABBInfosSBs[meshIdx].UploadBufferTypeData<AABB>(
			pDevice,
			pCmdList,
			resMesh.AABBs.size(),
			resMesh.AABBs.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		totalMeshletCount += meshletCount;
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

	struct MeshletMeshMaterial
	{
		uint32_t MeshIdx;
		uint32_t MaterialIdx;
	};

	// Meshlet描画用のカリング済みMeshletリストの生成
	if (!m_MeshletMeshMaterialTableSB.InitAsStructuredBuffer<MeshletMeshMaterial>
	(
		pDevice,
		totalMeshletCount,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		nullptr,
		L"DrawMeshletSB"
	))
	{
		ELOG("Error : Resource::InitAsByteAddressBuffe() Failed.");
		return false;
	}

	//TODO: 中身作成、Upload
	//m_DrawMeshletIndicesBBの作成がまだ

	m_pPoolGpuVisible = pPoolGpuVisible;
	m_pPoolGpuVisible->AddRef();

	m_pPoolCpuVisible = pPoolCpuVisible;
	m_pPoolCpuVisible->AddRef();

	return true;
}

void MeshManager::Term()
{
	for (Material* pMat : m_pMaterials)
	{
		if (pMat != nullptr)
		{
			pMat->Term();
		}
	}

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

	for (Resource& VB : m_VBs)
	{
		VB.Term();
	}
	m_VBs.clear();

	for (Resource& IB : m_IBs)
	{
		IB.Term();
	}
	m_IBs.clear();

	for (Resource& CB : m_CBs)
	{
		CB.Term();
	}
	m_CBs.clear();

	for (Resource& SB : m_MeshletsSBs)
	{
		SB.Term();
	}
	m_MeshletsSBs.clear();

	for (Resource& SB : m_MeshletsSBs)
	{
		SB.Term();
	}
	m_MeshletsSBs.clear();

	for (Resource& SB : m_MeshletsVerticesSBs)
	{
		SB.Term();
	}
	m_MeshletsVerticesSBs.clear();

	for (Resource& SB : m_MeshletsTrianglesSBs)
	{
		SB.Term();
	}
	m_MeshletsTrianglesSBs.clear();

	for (Resource& SB : m_MeshletsAABBInfosSBs)
	{
		SB.Term();
	}
	m_MeshletsAABBInfosSBs.clear();

	m_UnitCubeVB.Term();
	m_UnitCubeIB.Term();

	m_DrawMeshletIndirectArgBB.Term();
	m_MeshletMeshMaterialTableSB.Term();

}
