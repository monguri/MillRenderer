#include "MeshManager.h"
#include "DescriptorPool.h"
#include "Logger.h"
#include "App.h"
#include "FileUtil.h"

#include <DirectXHelpers.h>

using namespace DirectX::SimpleMath;

namespace
{
	// シェーダ側の定義と値の一致が必要
	static constexpr uint32_t MAX_MESH_COUNT = 256;

	struct alignas(256) CbMeshesDescHeapIndices
	{
		uint32_t CbMesh[MAX_MESH_COUNT];
		uint32_t SbVertexBuffer[MAX_MESH_COUNT];
		uint32_t SbMeshletBuffer[MAX_MESH_COUNT];
		uint32_t SbMeshletVerticesBuffer[MAX_MESH_COUNT];
		uint32_t SbMeshletTrianglesBuffer[MAX_MESH_COUNT];
		uint32_t SbMeshletAABBInfosBuffer[MAX_MESH_COUNT];
	};

	// シェーダ側の定義と値の一致が必要
	static constexpr uint32_t MAX_MATERIAL_COUNT = 256;

	struct alignas(256) CbMaterialsDescHeapIndices
	{
		uint32_t CbMaterial[MAX_MATERIAL_COUNT];
		uint32_t BaseColorMap[MAX_MATERIAL_COUNT];
		uint32_t MetallicRoughnessMap[MAX_MATERIAL_COUNT];
		uint32_t NormalMap[MAX_MATERIAL_COUNT];
		uint32_t EmissiveMap[MAX_MATERIAL_COUNT];
		uint32_t AOMap[MAX_MATERIAL_COUNT];
	};

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

	std::wstring ConvertToValidFilePath(const std::wstring& filePath)
	{
		std::wstring result;
		if (!SearchFilePathW(filePath.c_str(), result))
		{
			return std::wstring();
		}

		if (PathIsDirectoryW(result.c_str()) != FALSE)
		{
			return  std::wstring();
		}

		return result;
	}

	// シェーダはOpaqueでかつTwoSideでないもの用と、MaskedでTwoSideなもの用の2種類しか用意しないのでその他があれば弾く
	bool IsMaterialValid(const ResMaterial& resMat)
	{
		switch (resMat.AlphaMode)
		{
			case ALPHA_MODE_OPAQUE:
				return !resMat.DoubleSided;
			case ALPHA_MODE_MASK:
				return resMat.DoubleSided;
			case ALPHA_MODE_BLEND:
				return false;
			default:
				assert(false);
				return false;
		}
	}
}

MeshManager::~MeshManager()
{
	Term();
}

void MeshManager::Term()
{
	m_resMeshes.clear();
	m_resMaterials.clear();

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

	m_MeshesDescHeapIndicesCB.Term();
	m_MaterialsDescHeapIndicesCB.Term();

	for (Resource& CB : m_MeshCBs)
	{
		CB.Term();
	}
	m_MeshCBs.clear();

	for (Resource& VB : m_VBs)
	{
		VB.Term();
	}
	m_VBs.clear();

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

	m_MeshletMeshMaterialTableSB.Term();

	m_UnitCubeVB.Term();
	m_UnitCubeIB.Term();

	m_DrawOpaqueMeshletIndirectArgBB.Term();
	m_DrawOpaqueMeshletIndicesBB.Term();
	m_DrawMaskedMeshletIndirectArgBB.Term();
	m_DrawMaskedMeshletIndicesBB.Term();

	for (Resource& CB : m_MaterialCBs)
	{
		CB.Term();
	}
	m_MaterialCBs.clear();

	for (Texture& tex : m_BaseColorMaps)
	{
		tex.Term();
	}
	m_BaseColorMaps.clear();

	for (Texture& tex : m_MetallicRoughnessMaps)
	{
		tex.Term();
	}
	m_MetallicRoughnessMaps.clear();

	for (Texture& tex : m_NormalMaps)
	{
		tex.Term();
	}
	m_NormalMaps.clear();

	for (Texture& tex : m_EmissiveMaps)
	{
		tex.Term();
	}
	m_EmissiveMaps.clear();

	for (Texture& tex : m_AOMaps)
	{
		tex.Term();
	}
	m_AOMaps.clear();
}

bool MeshManager::RegisterModel(const std::wstring& filePath, const Matrix& worldMat, bool useMetis)
{
	std::vector<ResMesh> meshes;
	std::vector<ResMaterial> materials;

	if (!LoadMesh(filePath.c_str(), true, useMetis, meshes, materials))
	{
		ELOG("Error : Load Mesh Failed. filepath = %ls", filePath.c_str());
		return false;
	}

	uint32_t materialBaseIdx = static_cast<uint32_t>(m_resMaterials.size());
	for (ResMesh& mesh : meshes)
	{
		mesh.MaterialIdx += materialBaseIdx;
	}

	m_resMeshes.insert(m_resMeshes.end(), meshes.begin(), meshes.end());

	const std::wstring& dirPath = GetDirectoryPath(filePath.c_str());
	for (ResMaterial& material : materials)
	{
		material.DiffuseMap = dirPath + material.DiffuseMap;
		material.SpecularMap = dirPath + material.SpecularMap;
		material.ShininessMap = dirPath + material.ShininessMap;
		material.NormalMap = dirPath + material.NormalMap;
		material.HeightMap = dirPath + material.HeightMap;
		material.BaseColorMap = dirPath + material.BaseColorMap;
		// 以下のテクスチャはパス文字列が空かどうかはテクスチャが存在するかどうかのフラグになるので空の場合は空のままにしておく必要がある
		material.MetallicRoughnessMap = dirPath + material.MetallicRoughnessMap;
		if (!material.EmissiveMap.empty())
		{
			material.EmissiveMap = dirPath + material.EmissiveMap;
		}
		if (!material.AmbientOcclusionMap.empty())
		{
			material.AmbientOcclusionMap = dirPath + material.AmbientOcclusionMap;
		}
	}

	m_resMaterials.insert(m_resMaterials.end(), materials.begin(), materials.end());

	std::vector<Matrix> matrices;
	matrices.resize(meshes.size(), worldMat);
	m_worldMatrices.insert(m_worldMatrices.end(), matrices.begin(), matrices.end());

	return true;
}

bool MeshManager::Update(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue, ID3D12GraphicsCommandList* pCmdList, DescriptorPool* pPoolGpuVisible, DescriptorPool* pPoolCpuVisible, const Texture& dummyTexture)
{
	assert(pDevice != nullptr);
	assert(pQueue != nullptr);
	assert(pCmdList != nullptr);
	assert(pPoolGpuVisible != nullptr);
	assert(pPoolCpuVisible != nullptr);

	size_t meshCount = m_resMeshes.size();
	m_MeshCBs.resize(meshCount);
	m_VBs.resize(meshCount);
	m_MeshletsSBs.resize(meshCount);
	m_MeshletsVerticesSBs.resize(meshCount);
	m_MeshletsTrianglesSBs.resize(meshCount);
	m_MeshletsAABBInfosSBs.resize(meshCount);

	struct alignas(256) CbMesh
	{
		Matrix World;
		unsigned int MeshIdx;
		float Padding[3];
	};

	struct MeshletMeshMaterial
	{
		uint32_t MeshIdx;
		uint32_t MaterialIdx;
		uint32_t LocalMeshletIdx;
		uint32_t bMasked;
	};

	std::vector<MeshletMeshMaterial> meshletMeshMaterialTable;

	CbMeshesDescHeapIndices meshesDescHeapIndices = {};

	m_MeshletCount = 0;

	size_t validMeshIdx = 0;
	for (size_t meshIdx = 0; meshIdx < m_resMeshes.size(); meshIdx++)
	{
		const ResMesh& resMesh = m_resMeshes[meshIdx];

		const ResMaterial& resMat = m_resMaterials[resMesh.MaterialIdx];
		if (!IsMaterialValid(resMat))
		{
			continue;
		}

		size_t localMeshletCount = resMesh.Meshlets.size();

		// Worldへのフレーム遅延は発生するが今のところ遅延しても困る使い方はしてないので
		// 多重バッファにはしないでおく
		if (!m_MeshCBs[validMeshIdx].InitAsConstantBuffer<CbMesh>(
			pDevice,
			D3D12_HEAP_TYPE_DEFAULT,
			pPoolGpuVisible,
			L"CbMesh"
		))
		{
			ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
			return false;
		}

		CbMesh cbMesh = {m_worldMatrices[meshIdx], static_cast<uint32_t>(validMeshIdx)};
		if (!m_MeshCBs[validMeshIdx].UploadBufferTypeData<CbMesh>(
			pDevice,
			pCmdList,
			1,
			&cbMesh
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.CbMesh[validMeshIdx] = m_MeshCBs[validMeshIdx].GetHandleCBV()->GetDescriptorIndex();

		if (!m_VBs[validMeshIdx].InitAsStructuredBuffer<MeshVertex>(
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

		if (!m_VBs[validMeshIdx].UploadBufferTypeData<MeshVertex>(
			pDevice,
			pCmdList,
			resMesh.Vertices.size(),
			resMesh.Vertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbVertexBuffer[validMeshIdx] = m_VBs[validMeshIdx].GetHandleSRV()->GetDescriptorIndex();

		if (!m_MeshletsSBs[validMeshIdx].InitAsStructuredBuffer<meshopt_Meshlet>(
			pDevice,
			localMeshletCount,
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

		if (!m_MeshletsSBs[validMeshIdx].UploadBufferTypeData<meshopt_Meshlet>(
			pDevice,
			pCmdList,
			localMeshletCount,
			resMesh.Meshlets.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbMeshletBuffer[validMeshIdx] = m_MeshletsSBs[validMeshIdx].GetHandleSRV()->GetDescriptorIndex();

		if (!m_MeshletsVerticesSBs[validMeshIdx].InitAsStructuredBuffer<uint32_t>(
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

		if (!m_MeshletsVerticesSBs[validMeshIdx].UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			resMesh.MeshletsVertices.size(),
			resMesh.MeshletsVertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbMeshletVerticesBuffer[validMeshIdx] = m_MeshletsVerticesSBs[validMeshIdx].GetHandleSRV()->GetDescriptorIndex();

		// TODO: uint8_tの3つをuint32_tに詰め込んでBBで扱いたい。
		// 無駄にVRAMとメモリ帯域を使っている。Pixでの値確認はしやすいが。
		std::vector<uint32_t> meshletsTriangles;
		for (uint8_t index : resMesh.MeshletsTriangles)
		{
			meshletsTriangles.push_back(static_cast<uint32_t>(index));
		}

		if (!m_MeshletsTrianglesSBs[validMeshIdx].InitAsStructuredBuffer<uint32_t>(
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

		if (!m_MeshletsTrianglesSBs[validMeshIdx].UploadBufferTypeData<uint32_t>(
			pDevice,
			pCmdList,
			meshletsTriangles.size(),
			meshletsTriangles.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbMeshletTrianglesBuffer[validMeshIdx] = m_MeshletsTrianglesSBs[validMeshIdx].GetHandleSRV()->GetDescriptorIndex();

		assert(resMesh.AABBs.size() == localMeshletCount);

		if (!m_MeshletsAABBInfosSBs[validMeshIdx].InitAsStructuredBuffer<AABB>(
			pDevice,
			localMeshletCount,
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

		if (!m_MeshletsAABBInfosSBs[validMeshIdx].UploadBufferTypeData<AABB>(
			pDevice,
			pCmdList,
			resMesh.AABBs.size(),
			resMesh.AABBs.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbMeshletAABBInfosBuffer[validMeshIdx] = m_MeshletsAABBInfosSBs[validMeshIdx].GetHandleSRV()->GetDescriptorIndex();

		bool bMasked = (resMat.AlphaMode == ALPHA_MODE_MASK) && resMat.DoubleSided;
		for (size_t localMeshletIdx = 0; localMeshletIdx < localMeshletCount; localMeshletIdx++)
		{

			meshletMeshMaterialTable.emplace_back(static_cast<uint32_t>(validMeshIdx), resMesh.MaterialIdx, static_cast<uint32_t>(localMeshletIdx), bMasked ? 1 : 0);
		}

		m_MeshletCount += static_cast<uint32_t>(localMeshletCount);

		validMeshIdx++;
	}

	// MeshletとMeshおよびMaterialの対応テーブルの生成
	if (!m_MeshletMeshMaterialTableSB.InitAsStructuredBuffer<MeshletMeshMaterial>
	(
		pDevice,
		m_MeshletCount,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		nullptr,
		L"MeshletMeshMaterialTableSB"
	))
	{
		ELOG("Error : Resource::InitAsStructuredBuffer() Failed.");
		return false;
	}

	if (!m_MeshletMeshMaterialTableSB.UploadBufferTypeData<MeshletMeshMaterial>(
		pDevice,
		pCmdList,
		meshletMeshMaterialTable.size(),
		meshletMeshMaterialTable.data()
	))
	{
		ELOG("Error : Resource::UploadBufferTypeData() Failed.");
		return false;
	}

	std::vector<Vector3> cubeVertices;
	std::vector<uint32_t> cubeIndices;
	CreateUnitCubeMesh(cubeVertices, cubeIndices);

	if (!m_UnitCubeVB.InitAsVertexBuffer<Vector3>(
		pDevice,
		cubeVertices.size(),
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		L"UnitCubeVB"
	))
	{
		ELOG("Error : Resource::InitAsVertexBuffer() Failed.");
		return false;
	}

	if (!m_UnitCubeIB.InitAsIndexBuffer<uint32_t>(
		pDevice,
		DXGI_FORMAT_R32_UINT,
		cubeIndices.size(),
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		L"UnitCubeIB"
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

	// OpaqueのMeshlet描画用のMeshletカウンターのDispatchIndirectArgの生成
	if (!m_DrawOpaqueMeshletIndirectArgBB.InitAsByteAddressBuffer
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

	DirectX::TransitionResource(pCmdList, m_DrawOpaqueMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// OpaqueのMeshlet描画用のカリング済みMeshletIdxリストの生成
	if (!m_DrawOpaqueMeshletIndicesBB.InitAsByteAddressBuffer
	(
		pDevice,
		m_MeshletCount * sizeof(uint32_t),
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		pPoolGpuVisible,
		pPoolCpuVisible,
		L"DrawMeshletIndicesBB"
	))
	{
		ELOG("Error : Resource::InitAsByteAddressBuffe() Failed.");
		return false;
	}

	DirectX::TransitionResource(pCmdList, m_DrawOpaqueMeshletIndicesBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// MaskedのMeshlet描画用のMeshletカウンターのDispatchIndirectArgの生成
	if (!m_DrawMaskedMeshletIndirectArgBB.InitAsByteAddressBuffer
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

	DirectX::TransitionResource(pCmdList, m_DrawMaskedMeshletIndirectArgBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// MaskedのMeshlet描画用のカリング済みMeshletIdxリストの生成
	if (!m_DrawMaskedMeshletIndicesBB.InitAsByteAddressBuffer
	(
		pDevice,
		m_MeshletCount * sizeof(uint32_t),
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		pPoolGpuVisible,
		pPoolGpuVisible,
		pPoolCpuVisible,
		L"DrawMeshletIndicesBB"
	))
	{
		ELOG("Error : Resource::InitAsByteAddressBuffe() Failed.");
		return false;
	}

	DirectX::TransitionResource(pCmdList, m_DrawMaskedMeshletIndicesBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	if (!m_MeshesDescHeapIndicesCB.InitAsConstantBuffer<CbMeshesDescHeapIndices>(
		pDevice,
		D3D12_HEAP_TYPE_DEFAULT,
		pPoolGpuVisible,
		L"MeshesDescHeapIndicesCB"
	))
	{
		ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
		return false;
	}

	if (!m_MeshesDescHeapIndicesCB.UploadBufferTypeData<CbMeshesDescHeapIndices>(
		pDevice,
		pCmdList,
		1,
		&meshesDescHeapIndices
	))
	{
		ELOG("Error : Resource::UploadBufferTypeData() Failed.");
		return false;
	}

	CbMaterialsDescHeapIndices materialsDescHeapIndices = {};

	size_t materialCount = m_resMaterials.size();
	m_MaterialCBs.resize(materialCount);
	m_BaseColorMaps.resize(materialCount);
	m_MetallicRoughnessMaps.resize(materialCount);
	m_NormalMaps.resize(materialCount);
	m_EmissiveMaps.resize(materialCount);
	m_AOMaps.resize(materialCount);

	{
		DirectX::ResourceUploadBatch batch(pDevice);
		batch.Begin();

		struct alignas(256) CbMaterial
		{
			Vector3 BaseColorFactor;
			float MetallicFactor;
			float RoughnessFactor;
			Vector3 EmissiveFactor;
			unsigned int bAlphaMask;
			float AlphaCutoff;
			unsigned int bExistEmissiveTex;
			unsigned int bExistAOTex;
		};

		for (size_t materialIdx = 0; materialIdx < materialCount; materialIdx++)
		{
			const ResMaterial& resMat = m_resMaterials[materialIdx];
			// マテリアルの情報はResMeshのもつMaterialIdxから引っ張ってくるので
			// IsValidMaterial()によってはじくことはしない

			m_MaterialCBs[materialIdx].InitAsConstantBuffer<CbMaterial>(
				pDevice,
				D3D12_HEAP_TYPE_DEFAULT,
				pPoolGpuVisible,
				L"CbMaterial"
			);

			// 画像ファイルがディレクトリになかった場合はTextureを初期化しない。それを判定に用いてダミーテクスチャを使うようにする。他のテクスチャも同様
			std::wstring baseColorMapPath = ConvertToValidFilePath(resMat.BaseColorMap);
			if (baseColorMapPath.empty())
			{
				baseColorMapPath = ConvertToValidFilePath(resMat.DiffuseMap);
			}

			assert(!baseColorMapPath.empty());
			if (!m_BaseColorMaps[materialIdx].Init(pDevice, pPoolGpuVisible, baseColorMapPath.c_str(), true, batch))
			{
				ELOG("Error : Texture::Init() Failed. path = %ls", baseColorMapPath.c_str());
				return false;
			}

			const std::wstring& metallicRoughnessMapPath = ConvertToValidFilePath(resMat.MetallicRoughnessMap);
			if (!metallicRoughnessMapPath.empty())
			{
				if (!m_MetallicRoughnessMaps[materialIdx].Init(pDevice, pPoolGpuVisible, metallicRoughnessMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", metallicRoughnessMapPath.c_str());
					return false;
				}
			}

			const std::wstring& normalMapPath = ConvertToValidFilePath(resMat.NormalMap);
			if (!normalMapPath.empty())
			{
				if (!m_NormalMaps[materialIdx].Init(pDevice, pPoolGpuVisible, normalMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", normalMapPath.c_str());
					return false;
				}
			}

			const std::wstring& emissiveMapPath = ConvertToValidFilePath(resMat.EmissiveMap);
			if (!emissiveMapPath.empty())
			{
				if (!m_EmissiveMaps[materialIdx].Init(pDevice, pPoolGpuVisible, emissiveMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", emissiveMapPath.c_str());
					return false;
				}
			}

			const std::wstring& aoMapPath = ConvertToValidFilePath(resMat.AmbientOcclusionMap);
			if (!aoMapPath.empty())
			{
				if (!m_AOMaps[materialIdx].Init(pDevice, pPoolGpuVisible, aoMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", aoMapPath.c_str());
					return false;
				}
			}

			CbMaterial cbMat = {};
			cbMat.BaseColorFactor = resMat.BaseColor;
			cbMat.MetallicFactor = resMat.MetallicFactor;
			cbMat.RoughnessFactor = resMat.RoughnessFactor;
			cbMat.EmissiveFactor = resMat.EmissiveFactor;
			cbMat.bAlphaMask = resMat.DoubleSided ? 1 : 0;
			cbMat.AlphaCutoff = resMat.AlphaCutoff;
			cbMat.bExistEmissiveTex = resMat.EmissiveMap.empty() ? 0 : 1;
			cbMat.bExistAOTex = resMat.AmbientOcclusionMap.empty() ? 0 : 1;

			if (!m_MaterialCBs[materialIdx].UploadBufferTypeData<CbMaterial>(
				pDevice,
				pCmdList,
				1,
				&cbMat
			))
			{
				ELOG("Error : Resource::UploadBufferTypeData() Failed.");
				return false;
			}

			//materialsDescHeapIndicesへの代入
			uint32_t dummyTextureIndex = dummyTexture.GetHandleSRVPtr()->GetDescriptorIndex();

			// 画像ファイルがディレクトリになかった場合はTextureを初期化してない。その場合はダミーテクスチャを使う
			materialsDescHeapIndices.CbMaterial[materialIdx] = m_MaterialCBs[materialIdx].GetHandleCBV()->GetDescriptorIndex();
			materialsDescHeapIndices.BaseColorMap[materialIdx] = m_BaseColorMaps[materialIdx].GetHandleSRVPtr() == nullptr ? dummyTextureIndex : m_BaseColorMaps[materialIdx].GetHandleSRVPtr()->GetDescriptorIndex();
			materialsDescHeapIndices.MetallicRoughnessMap[materialIdx] = m_MetallicRoughnessMaps[materialIdx].GetHandleSRVPtr() == nullptr ? dummyTextureIndex : m_MetallicRoughnessMaps[materialIdx].GetHandleSRVPtr()->GetDescriptorIndex();
			materialsDescHeapIndices.NormalMap[materialIdx] = m_NormalMaps[materialIdx].GetHandleSRVPtr() == nullptr ? dummyTextureIndex : m_NormalMaps[materialIdx].GetHandleSRVPtr()->GetDescriptorIndex();
			materialsDescHeapIndices.EmissiveMap[materialIdx] = m_EmissiveMaps[materialIdx].GetHandleSRVPtr() == nullptr ? dummyTextureIndex : m_EmissiveMaps[materialIdx].GetHandleSRVPtr()->GetDescriptorIndex();
			materialsDescHeapIndices.AOMap[materialIdx] = m_AOMaps[materialIdx].GetHandleSRVPtr() == nullptr ? dummyTextureIndex : m_AOMaps[materialIdx].GetHandleSRVPtr()->GetDescriptorIndex();
		}

		std::future<void> future = batch.End(pQueue);
		future.wait();
	}

	if (!m_MaterialsDescHeapIndicesCB.InitAsConstantBuffer<CbMaterialsDescHeapIndices>(
		pDevice,
		D3D12_HEAP_TYPE_DEFAULT,
		pPoolGpuVisible,
		L"MaterialsDescHeapIndicesCB"
	))
	{
		ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
		return false;
	}

	if (!m_MaterialsDescHeapIndicesCB.UploadBufferTypeData<CbMaterialsDescHeapIndices>(
		pDevice,
		pCmdList,
		1,
		&materialsDescHeapIndices
	))
	{
		ELOG("Error : Resource::UploadBufferTypeData() Failed.");
		return false;
	}

	m_pPoolGpuVisible = pPoolGpuVisible;
	m_pPoolGpuVisible->AddRef();

	m_pPoolCpuVisible = pPoolCpuVisible;
	m_pPoolCpuVisible->AddRef();

	return true;
}

const Resource& MeshManager::GetDrawOpaqueMeshletIndirectArgBB() const
{
	return m_DrawOpaqueMeshletIndirectArgBB;
}

const Resource& MeshManager::GetDrawOpaqueMeshletIndicesBB() const
{
	return m_DrawOpaqueMeshletIndicesBB;
}

const Resource& MeshManager::GetDrawMaskedMeshletIndirectArgBB() const
{
	return m_DrawMaskedMeshletIndirectArgBB;
}

const Resource& MeshManager::GetDrawMaskedMeshletIndicesBB() const
{
	return m_DrawMaskedMeshletIndicesBB;
}

const Resource& MeshManager::GetMeshletMeshMaterialTableSB() const
{
	return m_MeshletMeshMaterialTableSB;
}

const Resource& MeshManager::GetMeshesDescHeapIndicesCB() const
{
	return m_MeshesDescHeapIndicesCB;
}

const Resource& MeshManager::GetMaterialsDescHeapIndicesCB() const
{
	return m_MaterialsDescHeapIndicesCB;
}

const Resource& MeshManager::GetUnitCubeVB() const
{
	return m_UnitCubeVB;
}

const Resource& MeshManager::GetUnitCubeIB() const
{
	return m_UnitCubeIB;
}

const ComPtr<ID3D12CommandSignature>& MeshManager::GetHWRasCmdSig() const
{
	return m_pDrawByHWRasCmdSig;
}

const ComPtr<ID3D12CommandSignature>& MeshManager::GetSWRasCmdSig() const
{
	return m_pDrawBySWRasCmdSig;
}

uint32_t MeshManager::GetMeshletCount() const
{
	return m_MeshletCount;
}

