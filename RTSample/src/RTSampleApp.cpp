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
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		inputs.NumDescs = 1;
		inputs.pGeometryDescs = &geomDesc;
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo;
		m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

		// ByteAddressBufferである必要は無いが必要な処理が揃っていたので
		if (!m_BlasScratchBB.Init(
			m_pDevice.Get(),
			nullptr,
			m_pPool[POOL_TYPE_RES],
			m_pPool[POOL_TYPE_RES],
			preBuildInfo.ScratchDataSizeInBytes,
			true,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}
		DirectX::TransitionResource(pCmd, m_BlasScratchBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (!m_BlasResultBB.Init(
			m_pDevice.Get(),
			nullptr,
			m_pPool[POOL_TYPE_RES],
			m_pPool[POOL_TYPE_RES],
			preBuildInfo.ResultDataMaxSizeInBytes,
			true,
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
		asDesc.ScratchAccelerationStructureData = m_BlasScratchBB.GetResource()->GetGPUVirtualAddress();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarrier.UAV.pResource = m_BlasResultBB.GetResource();
		pCmd->ResourceBarrier(1, &uavBarrier);
	}

	// TLASの作成
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		// あとでInstanceDescsを設定するので
		inputs.NumDescs = 1;
		inputs.pGeometryDescs = nullptr;
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo;
		m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

		// ByteAddressBufferである必要は無いが必要な処理が揃っていたので
		if (!m_TlasScratchBB.Init(
			m_pDevice.Get(),
			nullptr,
			m_pPool[POOL_TYPE_RES],
			m_pPool[POOL_TYPE_RES],
			preBuildInfo.ScratchDataSizeInBytes,
			true,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}
		DirectX::TransitionResource(pCmd, m_TlasScratchBB.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (!m_TlasResultBB.Init(
			m_pDevice.Get(),
			nullptr,
			m_pPool[POOL_TYPE_RES],
			m_pPool[POOL_TYPE_RES],
			preBuildInfo.ResultDataMaxSizeInBytes,
			true,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}

		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
		const Matrix& identityMat = Matrix::Identity;
		memcpy(instanceDesc.Transform, &identityMat, sizeof(instanceDesc.Transform));
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.AccelerationStructure = m_TlasResultBB.GetResource()->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

		if (!m_TlasInstanceDescBB.Init(
			m_pDevice.Get(),
			pCmd,
			m_pPool[POOL_TYPE_RES],
			m_pPool[POOL_TYPE_RES],
			sizeof(instanceDesc),
			true,
			D3D12_RESOURCE_STATE_COMMON,
			&instanceDesc
		))
		{
			ELOG("Error : StructuredBuffer::Init() Failed.");
			return false;
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc;
		asDesc.Inputs = inputs;
		asDesc.Inputs.InstanceDescs = m_TlasInstanceDescBB.GetResource()->GetGPUVirtualAddress();
		asDesc.DestAccelerationStructureData = m_TlasResultBB.GetResource()->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = m_TlasScratchBB.GetResource()->GetGPUVirtualAddress();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarrier.UAV.pResource = m_TlasResultBB.GetResource();
		pCmd->ResourceBarrier(1, &uavBarrier);
	}

	std::vector<D3D12_STATE_SUBOBJECT> subObjects;

	static const WCHAR* RAY_GEN_SHADER_ENTRY_NAME = L"rayGeneration";
	static const WCHAR* MISS_SHADER_ENTRY_NAME = L"miss";
	static const WCHAR* CLOSEST_HIT_SHADER_ENTRY_NAME = L"closestHit";

	// DXIL LibraryのSubObjectを作成
	D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
	{
		std::wstring lsPath;

		if (!SearchFilePath(L"SimpleRT.cso", lsPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pLSBlob;
		HRESULT hr = D3DReadFileToBlob(lsPath.c_str(), pLSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", lsPath.c_str());
			return false;
		}

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

		dxilLibDesc.DXILLibrary.pShaderBytecode = pLSBlob->GetBufferPointer();
		dxilLibDesc.DXILLibrary.BytecodeLength = pLSBlob->GetBufferSize();
		dxilLibDesc.NumExports = 3;
		dxilLibDesc.pExports = exportDescs;

		D3D12_STATE_SUBOBJECT subObjDxilLib;
		subObjDxilLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		subObjDxilLib.pDesc = &dxilLibDesc;
		subObjects.emplace_back(subObjDxilLib);
	}

	// HitGroupのSubObjectを作成
	D3D12_HIT_GROUP_DESC hitGroupDesc;
	{
		hitGroupDesc.HitGroupExport = L"HitGroup";
		hitGroupDesc.AnyHitShaderImport = nullptr;
		hitGroupDesc.ClosestHitShaderImport = L"closestHit";
		hitGroupDesc.IntersectionShaderImport = nullptr;

		D3D12_STATE_SUBOBJECT subObjHitGroup;
		subObjHitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		subObjHitGroup.pDesc = &hitGroupDesc;
		subObjects.emplace_back(subObjHitGroup);
	}

	// RayGenシェーダのLocal Root SignatureのSubObjectを作成
	RootSignature rayGenRootSig;
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetUAV(ShaderStage::ALL, 0, 0)
			.SetSRV(ShaderStage::ALL, 1, 0)
			.SetLocalRootSignature()
			.End();

		if (!rayGenRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed");
			return false;
		}

		D3D12_STATE_SUBOBJECT subObjLocalRootSig;
		subObjLocalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		// このタイプではID3D12RootSignature*を入れる
		subObjLocalRootSig.pDesc = rayGenRootSig.GetPtr();
		subObjects.emplace_back(subObjLocalRootSig);
	}
	
	// RayGenシェーダのExport AssociationのSubObjectを作成
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenExportsAssociation;
	{
		rayGenExportsAssociation.pSubobjectToAssociate = &subObjects.back();
		rayGenExportsAssociation.NumExports = 1;
		rayGenExportsAssociation.pExports = &RAY_GEN_SHADER_ENTRY_NAME;

		D3D12_STATE_SUBOBJECT subObjExportAssociation;
		subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subObjExportAssociation.pDesc = &rayGenExportsAssociation;
		subObjects.emplace_back(subObjExportAssociation);
	}

	// MissシェーダとClosestHitシェーダのLocal Root SignatureのSubObjectを作成
	// ルートシグネチャがひとつにまとめられるのでまとめている
	RootSignature missClosestHitGenRootSig;
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetLocalRootSignature()
			.End();

		if (!missClosestHitGenRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed");
			return false;
		}

		D3D12_STATE_SUBOBJECT subObjLocalRootSig;
		subObjLocalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		// このタイプではID3D12RootSignature*を入れる
		subObjLocalRootSig.pDesc = missClosestHitGenRootSig.GetPtr();
		subObjects.emplace_back(subObjLocalRootSig);
	}
	
	// MissシェーダとClosestHitシェーダのExport AssociationのSubObjectを作成
	// ルートシグネチャがひとつにまとめられるのでまとめている
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION missClosestHitExportsAssociation;
	const WCHAR* missClosestHitExportNames[] = {
		MISS_SHADER_ENTRY_NAME,
		CLOSEST_HIT_SHADER_ENTRY_NAME,
	};
	{
		missClosestHitExportsAssociation.pSubobjectToAssociate = &subObjects.back();
		missClosestHitExportsAssociation.NumExports = 2;
		missClosestHitExportsAssociation.pExports = missClosestHitExportNames;

		D3D12_STATE_SUBOBJECT subObjExportAssociation;
		subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subObjExportAssociation.pDesc = &missClosestHitExportsAssociation;
		subObjects.emplace_back(subObjExportAssociation);
	}

	// Shader Config（シェーダ間で受け渡すデータの上限サイズ情報）のSubObjectを作成
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	{
		// struct Payload
		// {
		//		bool hit;
		// };
		// boolも1バイト
		shaderConfig.MaxPayloadSizeInBytes = sizeof(bool);

		// struct BuiltInTriangleIntersectionAttributes
		// {
		//		float2 barycentrics;
		// };
		shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

		D3D12_STATE_SUBOBJECT subObjShaderConfig;
		subObjShaderConfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		subObjShaderConfig.pDesc = &shaderConfig;
		subObjects.emplace_back(subObjShaderConfig);
	}

	// ShaderConfigのExportAssociationのSubObjectを作成
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigExportsAssociation;
	const WCHAR* shaderConfigExportNames[] = {
		MISS_SHADER_ENTRY_NAME,
		CLOSEST_HIT_SHADER_ENTRY_NAME,
		RAY_GEN_SHADER_ENTRY_NAME,
	};
	{
		shaderConfigExportsAssociation.pSubobjectToAssociate = &subObjects.back();
		shaderConfigExportsAssociation.NumExports = 3;
		shaderConfigExportsAssociation.pExports = shaderConfigExportNames;

		D3D12_STATE_SUBOBJECT subObjExportAssociation;
		subObjExportAssociation.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subObjExportAssociation.pDesc = &shaderConfigExportsAssociation;
		subObjects.emplace_back(subObjExportAssociation);
	}

	// Global Root SignatureのSubObjectを作成
	RootSignature globalRootSig;
	{
		RootSignature::Desc desc;
		// GlobalRootSignatureの場合は何も設定しない
		desc.Begin().End();

		if (!globalRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed");
			return false;
		}

		D3D12_STATE_SUBOBJECT subObjGlobalRootSig;
		subObjGlobalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		// このタイプではID3D12RootSignature*を入れる
		subObjGlobalRootSig.pDesc = globalRootSig.GetPtr();
		subObjects.emplace_back(subObjGlobalRootSig);
	}

#if 0
	// RTPipelineのState Object作成
	ComPtr<ID3D12StateObject> pStateObject;
	{
		D3D12_STATE_OBJECT_DESC desc;
		desc.NumSubobjects = subObjects.size();
		desc.pSubobjects = subObjects.data();
		desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		HRESULT hr = m_pDevice->CreateStateObject(&desc, IID_PPV_ARGS(pStateObject.GetAddressOf()));
		if (FAILED(hr))
		{
			ELOG("Error : CreateStateObject() Failed");
			return false;
		}
	}
#endif

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
	m_BlasScratchBB.Term();
	m_BlasResultBB.Term();
	m_TlasScratchBB.Term();
	m_TlasResultBB.Term();
	m_TlasInstanceDescBB.Term();
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

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);

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

void RTSampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw BackBuffer");

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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
