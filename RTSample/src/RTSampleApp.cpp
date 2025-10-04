#include "RTSampleApp.h"

// imgui
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

// DirectX libraries
#include <DirectXMath.h>
#include <CommonStates.h>
#include <DirectXHelpers.h>

// Framework
#include "FileUtil.h"
#include "Logger.h"
#include "ScopedTimer.h"

#include <array>

//#define DYNAMIC_RESOURCES

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr float CAMERA_FOV_Y_DEGREE = 37.5f;
	static constexpr float CAMERA_NEAR = 0.1f;
	static constexpr float CAMERA_FAR = 100.0f;

	static constexpr Vector3 CAMERA_START_POSITION = Vector3(5.0f, 1.0f, 0.0f);
	static constexpr Vector3 CAMERA_START_TARGET = Vector3(0.0f, 1.0f, 0.0f);

	static constexpr uint32_t MAX_NUM_PARTICLES = 1024 * 1024;
	// シェーダ側と合わせている
	static const size_t NUM_THREAD_X = 64;

	struct alignas(256) CbCamera
	{
		Matrix View;
		Matrix Proj;
	};

	uint32_t DivideAndRoundUp(uint32_t dividend, uint32_t divisor)
	{
		return (dividend + divisor - 1) / divisor;
	}
}

RTSampleApp::RTSampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
{
}

RTSampleApp::~RTSampleApp()
{
}

bool RTSampleApp::OnInit(HWND hWnd)
{
	m_CameraManipulator.Reset(CAMERA_START_POSITION, CAMERA_START_TARGET);

	// imgui初期化
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		if (!ImGui_ImplWin32_Init(hWnd))
		{
			ELOG("Error : ImGui_ImplWin32_Init() Failed.");
			return false;
		}

		DescriptorHandle* pHandleSRV = m_pPool[POOL_TYPE_RES]->AllocHandle();
		if (pHandleSRV == nullptr)
		{
			ELOG("Error : DescriptorPool::AllocHandle() Failed.");
			return false;
		}

		if (!ImGui_ImplDX12_Init(m_pDevice.Get(), 1, m_BackBufferFormat, m_pPool[POOL_TYPE_RES]->GetHeap(), pHandleSRV->HandleCPU, pHandleSRV->HandleGPU))
		{
			ELOG("Error : ImGui_ImplDX12_Init() Failed.");
			return false;
		}
	}

	// シーン用デプスターゲットの生成
	{
		if (!m_SceneDepthTarget.Init
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_DSV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_D32_FLOAT,
			1.0f,
			0
		))
		{
			ELOG("Error : DepthTarget::Init() Failed.");
			return false;
		}
	}

	// カメラの定数バッファの作成
	{
		constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
		float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

		const Matrix& view = m_CameraManipulator.GetView();
		const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_CameraCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbCamera)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
			ptr->View = view;
			ptr->Proj = proj;
		}
	}

	// テスト用VBの作成
	{
		const Vector3 triangleVertices[3] = {
			Vector3(0, 1, 0),
			Vector3(0.866f, -0.5f, 0),
			Vector3(-0.866f, -0.5f, 0),
		};

		m_TriangleVB.Init<Vector3>(m_pDevice.Get(), 3, triangleVertices);
	}

	ID3D12GraphicsCommandList4* pCmd = m_CommandList.Reset();

	// BLASの作成
	ByteAddressBuffer blasScratchBB;
	{
		D3D12_RAYTRACING_GEOMETRY_DESC geomDesc;
		geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

		geomDesc.Triangles.VertexBuffer.StartAddress = m_TriangleVB.GetView().BufferLocation;
		geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vector3);
		geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geomDesc.Triangles.VertexCount = 3;
		// Transform3x4、IBの指定はオプション
		geomDesc.Triangles.Transform3x4 = 0;
		geomDesc.Triangles.IndexBuffer = 0;
		geomDesc.Triangles.IndexCount = 0;
		geomDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
		geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		inputs.NumDescs = 1;
		inputs.pGeometryDescs = &geomDesc;
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo;
		m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

		// ByteAddressBufferである必要は無いが必要な処理が揃っていたので
		if (!blasScratchBB.Init(
			m_pDevice.Get(),
			nullptr,
			nullptr,
			nullptr,
			preBuildInfo.ScratchDataSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}

		if (!m_BlasResultBB.Init(
			m_pDevice.Get(),
			nullptr,
			nullptr,
			nullptr,
			preBuildInfo.ResultDataMaxSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc;
		asDesc.Inputs = inputs;
		asDesc.DestAccelerationStructureData = m_BlasResultBB.GetResource()->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = blasScratchBB.GetResource()->GetGPUVirtualAddress();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarrier.UAV.pResource = m_BlasResultBB.GetResource();
		pCmd->ResourceBarrier(1, &uavBarrier);
	}

	// TLASの作成
	ByteAddressBuffer tlasScratchBB;
	ByteAddressBuffer tlasInstanceDescBB;
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		// あとでInstanceDescsを設定するので
		inputs.NumDescs = 1;
		inputs.pGeometryDescs = nullptr;
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo;
		m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

		// ByteAddressBufferである必要は無いが必要な処理が揃っていたので
		if (!tlasScratchBB.Init(
			m_pDevice.Get(),
			nullptr,
			nullptr,
			nullptr,
			preBuildInfo.ScratchDataSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}
		DirectX::TransitionResource(pCmd, tlasScratchBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (!m_TlasResultBB.Init(
			m_pDevice.Get(),
			nullptr,
			nullptr,
			nullptr,
			preBuildInfo.ResultDataMaxSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr
		))
		{
			ELOG("Error : ByteAddressBuffer::Init() Failed.");
			return false;
		}

		// TODO:ByteAddressBuffer::Init()でASのSRVに対応してないので一旦別途作る
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_TlasResultBB.GetResource()->GetGPUVirtualAddress();
		m_pTlasResultSrvHandle = m_pPool[POOL_TYPE_RES]->AllocHandle();
		if (m_pTlasResultSrvHandle == nullptr)
		{
			ELOG("Error : DescriptorPool::AllocHandle() Failed.");
			return false;
		}

		m_pDevice.Get()->CreateShaderResourceView(nullptr, &srvDesc, m_pTlasResultSrvHandle->HandleCPU);

		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
		const Matrix& identityMat = Matrix::Identity;
		memcpy(instanceDesc.Transform, &identityMat, sizeof(instanceDesc.Transform));
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.AccelerationStructure = m_TlasResultBB.GetResource()->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

		if (!tlasInstanceDescBB.Init(
			m_pDevice.Get(),
			pCmd,
			nullptr,
			nullptr,
			sizeof(instanceDesc),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			&instanceDesc
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc;
		asDesc.Inputs = inputs;
		asDesc.Inputs.InstanceDescs = tlasInstanceDescBB.GetResource()->GetGPUVirtualAddress();
		asDesc.DestAccelerationStructureData = m_TlasResultBB.GetResource()->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = tlasScratchBB.GetResource()->GetGPUVirtualAddress();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarrier.UAV.pResource = m_TlasResultBB.GetResource();
		pCmd->ResourceBarrier(1, &uavBarrier);
	}

	static const WCHAR* HIT_GROUP_NAME = L"HitGroup";
	static const WCHAR* RAY_GEN_SHADER_ENTRY_NAME = L"rayGeneration";
	static const WCHAR* MISS_SHADER_ENTRY_NAME = L"miss";
	static const WCHAR* CLOSEST_HIT_SHADER_ENTRY_NAME = L"closestHit";

	// State Objectの作成
	{
		static constexpr size_t SUB_OBJECT_COUNT = 10;
		// std::vectorだとExportAssociationでRootSigのSubObjectのポインタを取り出すのに
		// data()からのポインタオフセットが必要になり読みにくいので
		// TODO:std::vector.data()は試してない。&back()や&subObjects[subObjects.size() - 1]では
		// 不正アドレスアクセスでエラーになったがarrayと何が違うか把握できてない
		std::array<D3D12_STATE_SUBOBJECT, SUB_OBJECT_COUNT> subObjects;
		size_t subObjIdx = 0;


		// DXIL LibraryのSubObjectを作成
		D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
		D3D12_EXPORT_DESC exportDescs[3];
		exportDescs[0].Name = RAY_GEN_SHADER_ENTRY_NAME;
		exportDescs[0].ExportToRename = nullptr;
		exportDescs[0].Flags = D3D12_EXPORT_FLAG_NONE;
		exportDescs[1].Name = MISS_SHADER_ENTRY_NAME;
		exportDescs[1].ExportToRename = nullptr;
		exportDescs[1].Flags = D3D12_EXPORT_FLAG_NONE;
		exportDescs[2].Name = CLOSEST_HIT_SHADER_ENTRY_NAME;
		exportDescs[2].ExportToRename = nullptr;
		exportDescs[2].Flags = D3D12_EXPORT_FLAG_NONE;
		ComPtr<ID3DBlob> pLSBlob;
		{
			std::wstring lsPath;

			if (!SearchFilePath(L"SimpleRT.cso", lsPath))
			{
				ELOG("Error : Compute Shader Not Found");
				return false;
			}

			HRESULT hr = D3DReadFileToBlob(lsPath.c_str(), pLSBlob.GetAddressOf());
			if (FAILED(hr))
			{
				ELOG("Error : D3DReadFileToBlob Failed. path = %ls", lsPath.c_str());
				return false;
			}

			dxilLibDesc.DXILLibrary.pShaderBytecode = pLSBlob->GetBufferPointer();
			dxilLibDesc.DXILLibrary.BytecodeLength = pLSBlob->GetBufferSize();
			dxilLibDesc.NumExports = 3;
			dxilLibDesc.pExports = exportDescs;

			D3D12_STATE_SUBOBJECT subObjDxilLib;
			subObjDxilLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			subObjDxilLib.pDesc = &dxilLibDesc;
			subObjects[subObjIdx++] = subObjDxilLib;
		}

		// HitGroupのSubObjectを作成
		D3D12_HIT_GROUP_DESC hitGroupDesc;
		{
			hitGroupDesc.HitGroupExport = HIT_GROUP_NAME;
			hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			hitGroupDesc.AnyHitShaderImport = nullptr;
			hitGroupDesc.ClosestHitShaderImport = CLOSEST_HIT_SHADER_ENTRY_NAME;
			hitGroupDesc.IntersectionShaderImport = nullptr;

			D3D12_STATE_SUBOBJECT subObjHitGroup;
			subObjHitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			subObjHitGroup.pDesc = &hitGroupDesc;
			subObjects[subObjIdx++] = subObjHitGroup;
		}

		// RayGenシェーダのLocal Root SignatureのSubObjectを作成
		RootSignature rayGenRootSig;
		ID3D12RootSignature* pRayGenRootSig = nullptr;
		{
			RootSignature::Desc desc;
			desc.Begin()
				.SetSRV(ShaderStage::ALL, 0, 0)
				.SetUAV(ShaderStage::ALL, 1, 0)
				.SetLocalRootSignature()
				.End();

			if (!rayGenRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
			{
				ELOG("Error : RootSignature::Init() Failed");
				return false;
			}

			D3D12_STATE_SUBOBJECT subObjLocalRootSig;
			subObjLocalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			// なぜかID3D12RootSignature*の変数を
			pRayGenRootSig = rayGenRootSig.GetPtr();
			subObjLocalRootSig.pDesc = &pRayGenRootSig;
			subObjects[subObjIdx++] = subObjLocalRootSig;
		}
		
		// RayGenシェーダのExport AssociationのSubObjectを作成
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenExportsAssociation;
		{
			rayGenExportsAssociation.pSubobjectToAssociate = &(subObjects[subObjIdx - 1]);
			rayGenExportsAssociation.NumExports = 1;
			rayGenExportsAssociation.pExports = &RAY_GEN_SHADER_ENTRY_NAME;

			D3D12_STATE_SUBOBJECT subObjExportAssociation;
			subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			subObjExportAssociation.pDesc = &rayGenExportsAssociation;
			subObjects[subObjIdx++] = subObjExportAssociation;
		}

		// MissシェーダとClosestHitシェーダのLocal Root SignatureのSubObjectを作成
		// ルートシグネチャがひとつにまとめられるのでまとめている
		RootSignature missClosestHitRootSig;
		ID3D12RootSignature* pMissClosestHitRootSig = nullptr;
		{
			RootSignature::Desc desc;
			desc.Begin()
				.SetLocalRootSignature()
				.End();

			if (!missClosestHitRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
			{
				ELOG("Error : RootSignature::Init() Failed");
				return false;
			}

			D3D12_STATE_SUBOBJECT subObjLocalRootSig;
			subObjLocalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			pMissClosestHitRootSig = missClosestHitRootSig.GetPtr();
			subObjLocalRootSig.pDesc = &pMissClosestHitRootSig;
			subObjects[subObjIdx++] = subObjLocalRootSig;
		}
		
		// MissシェーダとClosestHitシェーダのExport AssociationのSubObjectを作成
		// ルートシグネチャがひとつにまとめられるのでまとめている
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missClosestHitExportsAssociation;
		const WCHAR* missClosestHitExportNames[] = {
			MISS_SHADER_ENTRY_NAME,
			CLOSEST_HIT_SHADER_ENTRY_NAME,
		};
		{
			missClosestHitExportsAssociation.pSubobjectToAssociate = &(subObjects[subObjIdx - 1]);
			missClosestHitExportsAssociation.NumExports = 2;
			missClosestHitExportsAssociation.pExports = missClosestHitExportNames;

			D3D12_STATE_SUBOBJECT subObjExportAssociation;
			subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			subObjExportAssociation.pDesc = &missClosestHitExportsAssociation;
			subObjects[subObjIdx++] = subObjExportAssociation;
		}

		// Shader Config（シェーダ間で受け渡すデータの上限サイズ情報）のSubObjectを作成
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
		{
			//struct Payload
			//{
			//		float3 color;
			//};
			shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 3;

			// struct BuiltInTriangleIntersectionAttributes
			// {
			//		float2 barycentrics;
			// };
			shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

			D3D12_STATE_SUBOBJECT subObjShaderConfig;
			subObjShaderConfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
			subObjShaderConfig.pDesc = &shaderConfig;
			subObjects[subObjIdx++] = subObjShaderConfig;
		}

		// ShaderConfigのExportAssociationのSubObjectを作成
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigExportsAssociation;
		const WCHAR* shaderConfigExportNames[] = {
			MISS_SHADER_ENTRY_NAME,
			CLOSEST_HIT_SHADER_ENTRY_NAME,
			RAY_GEN_SHADER_ENTRY_NAME,
		};
		{
			shaderConfigExportsAssociation.pSubobjectToAssociate = &(subObjects[subObjIdx - 1]);
			shaderConfigExportsAssociation.NumExports = 3;
			shaderConfigExportsAssociation.pExports = shaderConfigExportNames;

			D3D12_STATE_SUBOBJECT subObjExportAssociation;
			subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			subObjExportAssociation.pDesc = &shaderConfigExportsAssociation;
			subObjects[subObjIdx++] = subObjExportAssociation;
		}

		// Pipeline ConfigのSubObjectを作成
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
		{
			// 再帰的レイ生成は0段
			pipelineConfig.MaxTraceRecursionDepth = 0;

			D3D12_STATE_SUBOBJECT subObjPipelineConfig;
			subObjPipelineConfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
			subObjPipelineConfig.pDesc = &pipelineConfig;
			subObjects[subObjIdx++] = subObjPipelineConfig;
		}

		// Global Root SignatureのSubObjectを作成
		ID3D12RootSignature* pGlobalRootSig = nullptr;
		{
			RootSignature::Desc desc;
			// GlobalRootSignatureの場合は何も設定しない
			desc.Begin().End();

			if (!m_GlobalRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
			{
				ELOG("Error : RootSignature::Init() Failed");
				return false;
			}

			D3D12_STATE_SUBOBJECT subObjGlobalRootSig;
			subObjGlobalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
			pGlobalRootSig = m_GlobalRootSig.GetPtr();
			subObjGlobalRootSig.pDesc = &pGlobalRootSig;
			subObjects[subObjIdx++] = subObjGlobalRootSig;
		}

		assert(subObjIdx == SUB_OBJECT_COUNT);

		// RTPipelineのState Object作成
		{
			D3D12_STATE_OBJECT_DESC desc;
			desc.NumSubobjects = static_cast<UINT>(subObjects.size());
			desc.pSubobjects = subObjects.data();
			desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
			HRESULT hr = m_pDevice->CreateStateObject(&desc, IID_PPV_ARGS(m_pStateObject.GetAddressOf()));
			if (FAILED(hr))
			{
				ELOG("Error : CreateStateObject() Failed");
				return false;
			}
		}
	}
	// State Objectの作成

	// RT書き出し用テクスチャの作成
	// ここで作ったUAVがPOOL_TYPE_RESのディスクリプタプールでTLASのSRVの次に作るディスクリプタである必要がある
	// m_pTlasResultSrvHandleをRayGenのシェーダテーブルに設定するので
	{
		float clearColor[] = { 0, 0, 0, 0 };
		if (!m_RTTarget.InitUnorderedAccessTarget(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			nullptr,
			nullptr,
			m_Width,
			m_Height,
			// CopyResrouce()でバックバッファにコピーするのでフォーマットは同じでないと警告が出る
			m_BackBufferFormat,
			clearColor
		))
		{
			ELOG("Error : Texture::InitUnorderedAccessTarget() Failed");
			return false;
		}

		DirectX::TransitionResource(pCmd, m_RTTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	}

	// Shader Tableの作成
	{
		// 全シェーダ、最大サイズになるray-genシェーダに合わせる
		m_ShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		// デスクリプタテーブルの分
		m_ShaderTableEntrySize += 8 * 2;
		m_ShaderTableEntrySize = (m_ShaderTableEntrySize + D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1) / D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT * D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

		ComPtr<ID3D12StateObjectProperties> pStateObjProps;
		HRESULT hr = m_pStateObject->QueryInterface(IID_PPV_ARGS(pStateObjProps.GetAddressOf()));
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12StateObjectProperties::QueryInterface() Failed");
			return false;
		}

		// Map/Unmap()は現在のByteAddressBufferのD3D12_HEAP_TYPE_DEFAULTを使った実装では
		// 実行時エラーになるので別途アップロードバッファを使う書き込み方にする
		std::vector<uint8_t> shaderTblData;
		shaderTblData.resize(m_ShaderTableEntrySize * 3);
		memcpy(shaderTblData.data(), pStateObjProps->GetShaderIdentifier(RAY_GEN_SHADER_ENTRY_NAME), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		// RTではディスクリプタテーブルの設定をSetComputeRootDescriptorTable()ではなく
		// ShaderTableに設定する形で行う
		*(uint64_t*)(shaderTblData.data() + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = m_pTlasResultSrvHandle->HandleGPU.ptr;
		*(uint64_t*)(shaderTblData.data() + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8) = m_RTTarget.GetHandleUAVs()[0]->HandleGPU.ptr;
		memcpy(shaderTblData.data() + m_ShaderTableEntrySize, pStateObjProps->GetShaderIdentifier(MISS_SHADER_ENTRY_NAME), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		memcpy(shaderTblData.data() + m_ShaderTableEntrySize * 2, pStateObjProps->GetShaderIdentifier(HIT_GROUP_NAME), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		if (!m_ShaderTableBB.Init
		(
			m_pDevice.Get(),
			pCmd,
			nullptr,
			nullptr,
			shaderTblData.size(),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			shaderTblData.data()
		))
		{
			ELOG("Error : ByteAddressBuffer::Init() Failed");
			return false;
		}
	}

	//
	//
	//
	// TODO: そろそろ、各バッファは専用クラスをもつんでなく、ID3D12Resourceを内包する
	// Resoruceクラスを作って、InitBB、InitSBとかでBB、SB、CBなど作るやり方に
	// しないと無駄にいろんなクラスが乱立して実装が冗長になる
	// そもそもそれはD3D12の設計に合わない
	// テクスチャとバッファも別クラスにしなくていい
	//
	//
	//




	pCmd->Close();
	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);
	// Wait command queue finishing.
	m_Fence.Wait(m_pQueue.Get(), INFINITE);

	return true;
}

void RTSampleApp::OnTerm()
{
	// imgui終了処理
	if (ImGui::GetCurrentContext() != nullptr)
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_CameraCB[i].Term();
	}

	m_SceneDepthTarget.Term();

	m_TriangleVB.Term();
	m_BlasResultBB.Term();
	m_TlasResultBB.Term();
	m_ShaderTableBB.Term();
}

void RTSampleApp::OnRender()
{
	const Matrix& view = m_CameraManipulator.GetView();
	constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
	float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
	const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);

	// 定数バッファの更新
	{
		CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
		ptr->View = view;
		ptr->Proj = proj;
	}

	ID3D12GraphicsCommandList4* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);

	RayTrace(pCmd);
	DrawBackBuffer(pCmd);
	DrawImGui(pCmd);

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

bool RTSampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (ImGui::GetCurrentContext() != nullptr)
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
		{
			return false;
		}
	}

	// imguiウィンドウ内でマウスイベントを扱っているときは他のウィンドウでマウスイベントは扱わない
	if (ImGui::GetIO().WantCaptureMouse)
	{
		return false;
	}
	if (
		(msg == WM_KEYDOWN)
		|| (msg == WM_SYSKEYDOWN)
		|| (msg == WM_KEYUP)
		|| (msg == WM_SYSKEYUP)
	)
	{
		DWORD mask = (1 << 29);

		bool isKeyDown = ((msg == WM_KEYDOWN) || (msg == WM_SYSKEYDOWN));
		bool isAltDown = ((lp & mask) != 0);
		uint32_t keyCode = uint32_t(wp);

		if (isKeyDown)
		{
			switch (keyCode)
			{
				case VK_ESCAPE:
					PostQuitMessage(0);
					break;
				case 'C':
					m_CameraManipulator.Reset(CAMERA_START_POSITION, CAMERA_START_TARGET);
					break;
				default:
					break;
			}
		}
	}

	const UINT OLD_WM_MOUSEWHEEL = 0x020A;

	if (
		(msg == WM_LBUTTONDOWN)
		|| (msg == WM_LBUTTONUP)
		|| (msg == WM_LBUTTONDBLCLK)
		|| (msg == WM_MBUTTONDOWN)
		|| (msg == WM_MBUTTONUP)
		|| (msg == WM_MBUTTONDBLCLK)
		|| (msg == WM_RBUTTONDOWN)
		|| (msg == WM_RBUTTONUP)
		|| (msg == WM_RBUTTONDBLCLK)
		|| (msg == WM_XBUTTONDOWN)
		|| (msg == WM_XBUTTONUP)
		|| (msg == WM_XBUTTONDBLCLK)
		|| (msg == WM_MOUSEHWHEEL)
		|| (msg == WM_MOUSEMOVE)
		|| (msg == OLD_WM_MOUSEWHEEL)
	)
	{
		int x = int(LOWORD(lp));
		int y = int(HIWORD(lp));

		int delta = 0;
		if (msg == WM_MOUSEHWHEEL || msg == OLD_WM_MOUSEWHEEL)
		{
			POINT pt;
			pt.x = x;
			pt.y = y;

			ScreenToClient(hWnd, &pt);
			x = pt.x;
			y = pt.y;
		}

		int state = int(LOWORD(wp));
		bool left = ((state & MK_LBUTTON) != 0);
		bool right = ((state & MK_RBUTTON) != 0);
		bool middle = ((state & MK_MBUTTON) != 0);

		TransformManipulator::Event args = {};

		if (left)
		{
			args.Type = TransformManipulator::EventRotate;
			args.RotateH = DirectX::XMConvertToRadians(-0.5f * (x - m_PrevCursorX));
			args.RotateV = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_CameraManipulator.UpdateByEvent(args);
		}
		else if (right)
		{
			args.Type = TransformManipulator::EventDolly;
			args.Dolly = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_CameraManipulator.UpdateByEvent(args);
		}
		else if (middle)
		{
			args.Type = TransformManipulator::EventMove;
			if (GetAsyncKeyState(VK_MENU) != 0)
			{
				args.MoveX = DirectX::XMConvertToRadians(0.5f * (x - m_PrevCursorX));
				args.MoveZ = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			}
			else
			{
				args.MoveX = DirectX::XMConvertToRadians(0.5f * (x - m_PrevCursorX));
				args.MoveY = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			}
			m_CameraManipulator.UpdateByEvent(args);
		}

		m_PrevCursorX = x;
		m_PrevCursorY = y;
	}

	return true;
}

void RTSampleApp::RayTrace(ID3D12GraphicsCommandList4* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Ray Trace");

	DirectX::TransitionResource(pCmdList, m_RTTarget.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_DISPATCH_RAYS_DESC dispatchDesc;
	dispatchDesc.Width = m_Width;
	dispatchDesc.Height = m_Height;
	dispatchDesc.Depth = 1;

	dispatchDesc.RayGenerationShaderRecord.StartAddress = m_ShaderTableBB.GetResource()->GetGPUVirtualAddress() + 0 * m_ShaderTableEntrySize;
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_ShaderTableEntrySize;

	dispatchDesc.MissShaderTable.StartAddress = m_ShaderTableBB.GetResource()->GetGPUVirtualAddress() + 1 * m_ShaderTableEntrySize;
	dispatchDesc.MissShaderTable.StrideInBytes = m_ShaderTableEntrySize;
	dispatchDesc.MissShaderTable.SizeInBytes = m_ShaderTableEntrySize;

	dispatchDesc.HitGroupTable.StartAddress = m_ShaderTableBB.GetResource()->GetGPUVirtualAddress() + 2 * m_ShaderTableEntrySize;
	dispatchDesc.HitGroupTable.StrideInBytes = m_ShaderTableEntrySize;
	dispatchDesc.HitGroupTable.SizeInBytes = m_ShaderTableEntrySize;

	dispatchDesc.CallableShaderTable.StartAddress = 0;
	dispatchDesc.CallableShaderTable.StrideInBytes = 0;
	dispatchDesc.CallableShaderTable.SizeInBytes = 0;

	pCmdList->SetComputeRootSignature(m_GlobalRootSig.GetPtr());
	pCmdList->SetPipelineState1(m_pStateObject.Get());
	pCmdList->DispatchRays(&dispatchDesc);

	DirectX::TransitionResource(pCmdList, m_RTTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
}

void RTSampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw BackBuffer");

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

	pCmdList->CopyResource(m_ColorTarget[m_FrameIndex].GetResource(), m_RTTarget.GetResource());

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
}

void RTSampleApp::DrawImGui(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"ImGui");

	// TODO: Transitionが直前のパスと重複している
	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Render Settings");

	// imgui_demo.cppを参考にしている。右列のラベル部分のサイズを固定する
    ImGui::PushItemWidth(ImGui::GetFontSize() * -12);

#if 0
	ImGui::SliderInt("Num Spawn Per Frame", reinterpret_cast<int*>(& m_NumSpawnPerFrame), 1, 8192);
	ImGui::SliderInt("Initial Life", reinterpret_cast<int*>(& m_InitialLife), 1, 8192);
	ImGui::SliderFloat("Initial Velocity Scale", &m_InitialVelocityScale, 0.1f, 10.0f);
#endif

	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}
