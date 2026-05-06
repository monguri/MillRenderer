#include "MeshManager.h"
#include "DescriptorPool.h"
#include "ResMesh.h"
#include "Logger.h"
#include "App.h"
#include "FileUtil.h"

#include <DirectXHelpers.h>
#include <SimpleMath.h>

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

	struct alignas(256) CbMaterialsDescHeapIndices
	{
		uint32_t IsAlphaMask[MAX_MESH_COUNT];
		uint32_t CbMaterial[MAX_MESH_COUNT];
		uint32_t BaseColorMap[MAX_MESH_COUNT];
		uint32_t MetallicRoughnessMap[MAX_MESH_COUNT];
		uint32_t NormalMap[MAX_MESH_COUNT];
		uint32_t EmissiveMap[MAX_MESH_COUNT];
		uint32_t AOMap[MAX_MESH_COUNT];
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
}

MeshManager::~MeshManager()
{
	Term();
}

bool MeshManager::Init
(
	ID3D12Device* pDevice,
	ID3D12CommandQueue* pQueue,
	ID3D12GraphicsCommandList* pCmdList,
	class DescriptorPool* pPoolGpuVisible,
	class DescriptorPool* pPoolCpuVisible,
	const std::wstring& modelDirPath,
	const std::vector<struct ResMesh>& resMeshes,
	const std::vector<struct ResMaterial>& resMaterials,
	const Texture& dummyTexture
)
{
	assert(pDevice != nullptr);
	assert(pQueue != nullptr);
	assert(pCmdList != nullptr);
	assert(pPoolGpuVisible != nullptr);
	assert(pPoolCpuVisible != nullptr);

	size_t meshCount = resMeshes.size();
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
	};

	std::vector<MeshletMeshMaterial> meshletMeshMaterialTable;

	CbMeshesDescHeapIndices meshesDescHeapIndices = {};

	size_t totalMeshletCount = 0;

	for (size_t meshIdx = 0; meshIdx < meshCount; meshIdx++)
	{
		const ResMesh& resMesh = resMeshes[meshIdx];
		size_t meshletCount = resMesh.Meshlets.size();

		// Worldへのフレーム遅延は発生するが今のところ遅延しても困る使い方はしてないので
		// 多重バッファにはしないでおく
		if (!m_MeshCBs[meshIdx].InitAsConstantBuffer<CbMesh>(
			pDevice,
			D3D12_HEAP_TYPE_DEFAULT,
			pPoolGpuVisible,
			L"CbMesh"
		))
		{
			ELOG("Error : Resource::InitAsConstantBuffer() Failed.");
			return false;
		}

		CbMesh cbMesh = {Matrix::Identity, static_cast<uint32_t>(meshIdx)};
		if (!m_MeshCBs[meshIdx].UploadBufferTypeData<CbMesh>(
			pDevice,
			pCmdList,
			1,
			&cbMesh
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.CbMesh[meshIdx] = m_MeshCBs[meshIdx].GetHandleCBV()->GetDescriptorIndex();

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

		if (!m_VBs[meshIdx].UploadBufferTypeData<MeshVertex>(
			pDevice,
			pCmdList,
			resMesh.Vertices.size(),
			resMesh.Vertices.data()
		))
		{
			ELOG("Error : Resource::UploadBufferTypeData() Failed.");
			return false;
		}

		meshesDescHeapIndices.SbVertexBuffer[meshIdx] = m_VBs[meshIdx].GetHandleSRV()->GetDescriptorIndex();

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

		meshesDescHeapIndices.SbMeshletBuffer[meshIdx] = m_MeshletsSBs[meshIdx].GetHandleSRV()->GetDescriptorIndex();

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

		meshesDescHeapIndices.SbMeshletVerticesBuffer[meshIdx] = m_MeshletsVerticesSBs[meshIdx].GetHandleSRV()->GetDescriptorIndex();

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

		meshesDescHeapIndices.SbMeshletTrianglesBuffer[meshIdx] = m_MeshletsTrianglesSBs[meshIdx].GetHandleSRV()->GetDescriptorIndex();

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

		meshesDescHeapIndices.SbMeshletAABBInfosBuffer[meshIdx] = m_MeshletsAABBInfosSBs[meshIdx].GetHandleSRV()->GetDescriptorIndex();

		for (size_t i = 0; i < meshletCount; i++)
		{
			meshletMeshMaterialTable.emplace_back(static_cast<uint32_t>(meshIdx), resMesh.MaterialIdx);
		}

		totalMeshletCount += meshletCount;
	}

	// MeshletとMeshおよびMaterialの対応テーブルの生成
	if (!m_MeshletMeshMaterialTableSB.InitAsStructuredBuffer<MeshletMeshMaterial>
	(
		pDevice,
		totalMeshletCount,
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
	if (!m_DrawMeshletIndicesBB.InitAsByteAddressBuffer
	(
		pDevice,
		totalMeshletCount * sizeof(uint32_t),
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

	DirectX::TransitionResource(pCmdList, m_DrawMeshletIndicesBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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

	size_t materialCount = resMaterials.size();
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
			float AlphaCutoff;
			int bExistEmissiveTex;
			int bExistAOTex;
			// GBuffer用のPSOに割り振るIDだが一般にMaterialIDという用語なのでそうしている
			unsigned int MaterialID;
		};

		for (size_t materialIdx = 0; materialIdx < materialCount; materialIdx++)
		{
			const ResMaterial& resMat = resMaterials[materialIdx];

			m_MaterialCBs[materialIdx].InitAsConstantBuffer<CbMaterial>(
				pDevice,
				D3D12_HEAP_TYPE_DEFAULT,
				pPoolGpuVisible,
				L"CbMaterial"
			);

			// 画像ファイルがディレクトリになかった場合はTextureを初期化しない。それを判定に用いてダミーテクスチャを使うようにする。他のテクスチャも同様
			std::wstring baseColorMapPath = ConvertToValidFilePath(modelDirPath + resMat.BaseColorMap);
			if (baseColorMapPath.empty())
			{
				baseColorMapPath = ConvertToValidFilePath(modelDirPath + resMat.DiffuseMap);
			}

			if (!baseColorMapPath.empty())
			{
				if (!m_BaseColorMaps[materialIdx].Init(pDevice, pPoolGpuVisible, baseColorMapPath.c_str(), true, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", baseColorMapPath.c_str());
					return false;
				}
			}

			const std::wstring& metallicRoughnessMapPath = ConvertToValidFilePath(modelDirPath + resMat.MetallicRoughnessMap);
			if (!metallicRoughnessMapPath.empty())
			{
				if (!m_MetallicRoughnessMaps[materialIdx].Init(pDevice, pPoolGpuVisible, metallicRoughnessMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", metallicRoughnessMapPath.c_str());
					return false;
				}
			}

			const std::wstring& normalMapPath = ConvertToValidFilePath(modelDirPath + resMat.NormalMap);
			if (!normalMapPath.empty())
			{
				if (!m_NormalMaps[materialIdx].Init(pDevice, pPoolGpuVisible, normalMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", normalMapPath.c_str());
					return false;
				}
			}

			const std::wstring& emissiveMapPath = ConvertToValidFilePath(modelDirPath + resMat.EmissiveMap);
			if (!emissiveMapPath.empty())
			{
				if (!m_EmissiveMaps[materialIdx].Init(pDevice, pPoolGpuVisible, emissiveMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed. path = %ls", emissiveMapPath.c_str());
					return false;
				}
			}

			const std::wstring& aoMapPath = ConvertToValidFilePath(modelDirPath + resMat.AmbientOcclusionMap);
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
			cbMat.AlphaCutoff = resMat.AlphaCutoff;
			cbMat.bExistEmissiveTex = resMat.EmissiveMap.empty() ? 0 : 1;
			cbMat.bExistAOTex = resMat.AmbientOcclusionMap.empty() ? 0 : 1;
			// 現状、DynamicResourceを考慮するとGBuffer描画には一種類のパイプラインしか使っていない
			cbMat.MaterialID = 0;

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
			materialsDescHeapIndices.IsAlphaMask[materialIdx] = resMat.DoubleSided ? 0 : 1;
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

void MeshManager::Term()
{
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

	m_DrawMeshletIndirectArgBB.Term();
	m_DrawMeshletIndicesBB.Term();

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

void MeshManager::ClearDrawMeshletBBs(ID3D12GraphicsCommandList6* pCmdList) const
{
	uint32_t clearValue[4] = {0, 0, 0, 0};
	// 本来はX=0、Y=1、Z=1にしたいが、ClearUavWithUintValue()とByteAddressBufferではそれができないようだ。[0]の値ですべてクリアされてしまう。よってY=1、Z=1はシェーダで入れる。
	m_DrawMeshletIndirectArgBB.ClearUavWithUintValue(pCmdList, clearValue);
	m_DrawMeshletIndicesBB.ClearUavWithUintValue(pCmdList, clearValue);

	m_DrawMeshletIndirectArgBB.BarrierUAV(pCmdList);
	m_DrawMeshletIndicesBB.BarrierUAV(pCmdList);
}

void MeshManager::DoCulling(ID3D12GraphicsCommandList6* pCmdList) const
{
	//TODO:実装
}

const Resource& MeshManager::GetDrawMeshletIndirectArgBB() const
{
	return m_DrawMeshletIndirectArgBB;
}

const Resource& MeshManager::GetDrawMeshletIndicesBB() const
{
	return m_DrawMeshletIndicesBB;
}

const Resource& MeshManager::GetMeshletMeshMaterialTableSB() const
{
	return m_MeshletMeshMaterialTableSB;
}

const Resource& MeshManager::GetMeshesDescHeapIndicesCB() const
{
	return m_MeshesDescHeapIndicesCB;
}

uint32_t MeshManager::GetMeshletCount() const
{
	return m_MeshletCount;
}

