#include "SWTessSampleApp.h"

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

	struct ParticleData
	{
		Vector3 Position;
		Vector3 Velocity;
		uint32_t Life;
	};

	struct alignas(256) CbSimulation
	{
		float DeltaTime;
		float InitialVelocityScale;
		Vector2 Dummy;
	};

	struct alignas(256) CbSampleTexture
	{
		int bOnlyRedChannel;
		float Contrast;
		float Scale;
		float Bias;
	};

	uint32_t DivideAndRoundUp(uint32_t dividend, uint32_t divisor)
	{
		return (dividend + divisor - 1) / divisor;
	}
}

SWTessSampleApp::SWTessSampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
{
}

SWTessSampleApp::~SWTessSampleApp()
{
}

bool SWTessSampleApp::OnInit(HWND hWnd)
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

#ifdef DYNAMIC_RESOURCES
	// Dynamic Resourcesを使うためには、Resource Binding Tier 3以上、Shader Model 6.6以上が必要
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS featureOptions{};
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
		shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
		HRESULT hr = m_pDevice.Get()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureOptions, sizeof(featureOptions));
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CheckFeatureSupport() Failed.");
			return false;
		}

		if (featureOptions.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3)
		{
			ELOG("Error : D3D12_RESOURCE_BINDING_TIER_3 not suppoted.");
			return false;
		}

		hr = m_pDevice.Get()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CheckFeatureSupport() Failed.");
			return false;
		}

		if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6)
		{
			ELOG("Error : D3D_SHADER_MODEL_6_6 not suppoted.");
			return false;
		}
	}
#endif // #ifdef DYNAMIC_RESOUCES

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

	// パーティクル描画用カラーターゲットの生成
	{
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		if (!m_DrawParticlesTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			m_ColorTarget[0].GetRTVDesc().Format,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// パーティクル数リセット用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"ResetNumParticles.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_ResetNumParticlesRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_ResetNumParticlesRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pResetNumParticlesPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// パーティクル更新用コマンドシグニチャの生成
	{
		D3D12_INDIRECT_ARGUMENT_DESC argDesc;
		argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

		D3D12_COMMAND_SIGNATURE_DESC csDesc;
		csDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
		csDesc.NodeMask = 1;
		csDesc.NumArgumentDescs = 1;
		csDesc.pArgumentDescs = &argDesc;

		HRESULT hr = m_pDevice->CreateCommandSignature(
			&csDesc,
			nullptr,
			IID_PPV_ARGS(m_pUpdateParticlesCommandSig.GetAddressOf()));
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateCommandSignature Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// パーティクル更新用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"UpdateParticlesCS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_UpdateParticlesRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_UpdateParticlesRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pUpdateParticlesPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// パーティクル描画用ルートシグニチャとコマンドシグネチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"DrawParticlesVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"DrawParticlesPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_DrawParticlesRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout.NumElements = 0;
		desc.InputLayout.pInputElementDescs = nullptr;
		desc.pRootSignature = m_DrawParticlesRootSig.GetPtr();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_DrawParticlesTarget.GetRTVDesc().Format;
		desc.DSVFormat = m_SceneDepthTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pDrawParticlesPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// パーティクル描画用コマンドシグニチャの生成
	{
		D3D12_INDIRECT_ARGUMENT_DESC argDesc;
		argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

		D3D12_COMMAND_SIGNATURE_DESC csDesc;
		csDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
		csDesc.NodeMask = 1;
		csDesc.NumArgumentDescs = 1;
		csDesc.pArgumentDescs = &argDesc;

		HRESULT hr = m_pDevice->CreateCommandSignature(
			&csDesc,
			nullptr,
			IID_PPV_ARGS(m_pDrawParticlesCommandSig.GetAddressOf()));
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateCommandSignature Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// スクリーンスペース描画パス用のInputElement。解放されないようにスコープ外で定義。
	D3D12_INPUT_ELEMENT_DESC SSPassInputElements[2];
	SSPassInputElements[0].SemanticName = "POSITION";
	SSPassInputElements[0].SemanticIndex = 0;
	SSPassInputElements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
	SSPassInputElements[0].InputSlot = 0;
	SSPassInputElements[0].AlignedByteOffset = 0;
	SSPassInputElements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	SSPassInputElements[0].InstanceDataStepRate = 0;

	SSPassInputElements[1].SemanticName = "TEXCOORD";
	SSPassInputElements[1].SemanticIndex = 0;
	SSPassInputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	SSPassInputElements[1].InputSlot = 0;
	SSPassInputElements[1].AlignedByteOffset = 8;
	SSPassInputElements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	SSPassInputElements[1].InstanceDataStepRate = 0;

	// スクリーンスペース描画パス用のD3D12_GRAPHICS_PIPELINE_STATE_DESCの共通項
	D3D12_GRAPHICS_PIPELINE_STATE_DESC SSPassPSODescCommon = {};
	{
		SSPassPSODescCommon.InputLayout.pInputElementDescs = SSPassInputElements;
		SSPassPSODescCommon.InputLayout.NumElements = 2;
		SSPassPSODescCommon.pRootSignature = nullptr; // 上書き必須
		SSPassPSODescCommon.VS.pShaderBytecode = nullptr; // 上書き必須。TODO:使いまわそうとしたらエラーになった。
		SSPassPSODescCommon.VS.BytecodeLength = 0; // 上書き必須。TODO:使いまわそうとしたらエラーになった。
		SSPassPSODescCommon.PS.pShaderBytecode = nullptr; // 上書き必須
		SSPassPSODescCommon.PS.BytecodeLength = 0; // 上書き必須
		SSPassPSODescCommon.RasterizerState = DirectX::CommonStates::CullCounterClockwise;
		SSPassPSODescCommon.BlendState = DirectX::CommonStates::Opaque;
		SSPassPSODescCommon.DepthStencilState = DirectX::CommonStates::DepthNone;
		SSPassPSODescCommon.SampleMask = UINT_MAX;
		SSPassPSODescCommon.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		SSPassPSODescCommon.NumRenderTargets = 1;
		SSPassPSODescCommon.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 上書き必須
		SSPassPSODescCommon.SampleDesc.Count = 1;
		SSPassPSODescCommon.SampleDesc.Quality = 0;
	}

    // バックバッファ描画用ルートシグニチャとパイプラインステートの生成
	// DXGIフォーマットを指定する必要があるので一般のテクスチャコピー用にはできなかった
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"SampleTexturePS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_BackBufferRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_BackBufferRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pBackBufferPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// スクリーンスペースパス用頂点バッファの生成
	{
		struct Vertex
		{
			float px;
			float py;
			float tx;
			float ty;
		};

		if (!m_QuadVB.Init<Vertex>(m_pDevice.Get(), 3 * sizeof(Vertex)))
		{
			ELOG("Error : VertexBuffer::Init Failed.");
			return false;
		}

		Vertex* ptr = m_QuadVB.Map<Vertex>();
		assert(ptr != nullptr);
		ptr[0].px = -1.0f; ptr[0].py = 1.0f; ptr[0].tx = 0.0f; ptr[0].ty = 0.0f;
		ptr[1].px = 3.0f; ptr[1].py = 1.0f; ptr[1].tx = 2.0f; ptr[1].ty = 0.0f;
		ptr[2].px = -1.0f; ptr[2].py = -3.0f; ptr[2].tx = 0.0f; ptr[2].ty = 2.0f;
		m_QuadVB.Unmap();
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

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	// パーティクル更新用のDispatchIndirectArgsBufferの作成
	{
		D3D12_DISPATCH_ARGUMENTS args;
		args.ThreadGroupCountX = (m_NumSpawnPerFrame + NUM_THREAD_X - 1) / NUM_THREAD_X;
		args.ThreadGroupCountY = 1;
		args.ThreadGroupCountZ = 1;

		if (!m_DispatchIndirectArgsBB.Init(m_pDevice.Get(), pCmd, m_pPool[POOL_TYPE_RES], m_pPool[POOL_TYPE_RES], sizeof(args) / sizeof(uint32_t), true, &args))
		{
			ELOG("Error : ByteAddressBuffer::Init() Failed.");
			return false;
		}
	}

	// パーティクル用のStructuredBufferの作成
	{
		for (uint32_t i = 0; i < FRAME_COUNT; i++)
		{
			if (!m_ParticlesSB[i].Init<ParticleData>(m_pDevice.Get(), pCmd, m_pPool[POOL_TYPE_RES], m_pPool[POOL_TYPE_RES], MAX_NUM_PARTICLES, true, nullptr))
			{
				ELOG("Error : StructuredBuffer::Init() Failed.");
				return false;
			}
			DirectX::TransitionResource(pCmd, m_ParticlesSB[i].GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}

	// パーティクル描画用のDrawIndirectArgsBufferの作成
	{
		D3D12_DRAW_ARGUMENTS args;
		args.VertexCountPerInstance = 4;
		args.InstanceCount = 0;
		args.StartVertexLocation = 0;
		args.StartInstanceLocation = 0;

		for (uint32_t i = 0; i < FRAME_COUNT; i++)
		{
			if (!m_DrawParticlesIndirectArgsBB[i].Init(m_pDevice.Get(), pCmd, m_pPool[POOL_TYPE_RES], m_pPool[POOL_TYPE_RES], sizeof(args) / sizeof(uint32_t), true, &args))
			{
				ELOG("Error : ByteAddressBuffer::Init() Failed.");
				return false;
			}
		}
	}

	// 時間関係の定数バッファの作成
	{
		if (!m_SimulationCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSimulation)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSimulation* ptr = m_SimulationCB.GetPtr<CbSimulation>();
		ptr->DeltaTime = 0.0f;
		ptr->InitialVelocityScale = 1.0f;
	}

	// バックバッファ描画用の定数バッファの作成
	{
		if (!m_BackBufferCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSampleTexture)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSampleTexture* ptr = m_BackBufferCB.GetPtr<CbSampleTexture>();
		ptr->bOnlyRedChannel = 0;
		ptr->Contrast = 1.0f;
		ptr->Scale = 1.0f;
		ptr->Bias = 0.0f;
	}

	pCmd->Close();
	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);
	// Wait command queue finishing.
	m_Fence.Wait(m_pQueue.Get(), INFINITE);

	m_PrevTime = std::chrono::high_resolution_clock::now();

	return true;
}

void SWTessSampleApp::OnTerm()
{
	// imgui終了処理
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	m_QuadVB.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_CameraCB[i].Term();
		m_ParticlesSB[i].Term();
		m_DrawParticlesIndirectArgsBB[i].Term();
	}

	m_DispatchIndirectArgsBB.Term();

	m_SimulationCB.Term();
	m_BackBufferCB.Term();

	m_SceneDepthTarget.Term();
	m_DrawParticlesTarget.Term();

	m_pResetNumParticlesPSO.Reset();
	m_ResetNumParticlesRootSig.Term();

	m_pUpdateParticlesCommandSig.Reset();
	m_pUpdateParticlesPSO.Reset();
	m_UpdateParticlesRootSig.Term();

	m_pDrawParticlesPSO.Reset();
	m_DrawParticlesRootSig.Term();
	m_pDrawParticlesCommandSig.Reset();

	m_pBackBufferPSO.Reset();
	m_BackBufferRootSig.Term();
}

void SWTessSampleApp::OnRender()
{
	using namespace std::chrono;
	const high_resolution_clock::time_point& currTime = high_resolution_clock::now();
	const milliseconds& deltaTimeMS = duration_cast<milliseconds>(currTime - m_PrevTime);
	m_PrevTime = currTime;

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

	const StructuredBuffer& prevParticlesSB = m_ParticlesSB[m_FrameIndex];
	const StructuredBuffer& currParticlesSB = m_ParticlesSB[(m_FrameIndex + 1) % 2];
	const ByteAddressBuffer& prevDrawParticlesArgsBB = m_DrawParticlesIndirectArgsBB[m_FrameIndex];
	const ByteAddressBuffer& currDrawParticlesArgsBB = m_DrawParticlesIndirectArgsBB[(m_FrameIndex + 1) % 2];

	ResetNumParticles(pCmd, prevDrawParticlesArgsBB, currDrawParticlesArgsBB);
	UpdateParticles(pCmd, prevParticlesSB, currParticlesSB, prevDrawParticlesArgsBB, currDrawParticlesArgsBB, deltaTimeMS);
	DrawParticles(pCmd, currParticlesSB, currDrawParticlesArgsBB);

	DrawBackBuffer(pCmd);

	DrawImGui(pCmd);

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

bool SWTessSampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
	{
		return false;
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

void SWTessSampleApp::ResetNumParticles(ID3D12GraphicsCommandList* pCmdList, const ByteAddressBuffer& prevDrawParticlesArgsBB, const ByteAddressBuffer& currDrawParticlesArgsBB)
{
	ScopedTimer scopedTimer(pCmdList, L"Reset Num Particles");

	pCmdList->SetComputeRootSignature(m_ResetNumParticlesRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pResetNumParticlesPSO.Get());

	pCmdList->SetComputeRoot32BitConstant(0, m_NumSpawnPerFrame, 0);
	pCmdList->SetComputeRootDescriptorTable(1, prevDrawParticlesArgsBB.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, currDrawParticlesArgsBB.GetHandleUAV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, m_DispatchIndirectArgsBB.GetHandleUAV()->HandleGPU);
	pCmdList->Dispatch(1, 1, 1);
}

void SWTessSampleApp::UpdateParticles(ID3D12GraphicsCommandList* pCmdList, const StructuredBuffer& prevParticlesSB, const StructuredBuffer& currParticlesSB, const ByteAddressBuffer& prevDrawParticlesArgsBB, const ByteAddressBuffer& currDrawParticlesArgsBB, const std::chrono::milliseconds& deltaTimeMS)
{
	ScopedTimer scopedTimer(pCmdList, L"Update Particles");

	// 定数バッファの更新
	{
		CbSimulation* ptr = m_SimulationCB.GetPtr<CbSimulation>();
		ptr->DeltaTime = deltaTimeMS.count() / 1000.0f;
		ptr->InitialVelocityScale = m_InitialVelocityScale;
	}

	DirectX::TransitionResource(pCmdList, prevParticlesSB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	pCmdList->SetComputeRootSignature(m_UpdateParticlesRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pUpdateParticlesPSO.Get());

	uint32_t rootConstants[2] = {m_NumSpawnPerFrame, m_InitialLife};
	pCmdList->SetComputeRoot32BitConstants(0, 2, rootConstants, 0);

	pCmdList->SetComputeRootDescriptorTable(1, m_SimulationCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(2, prevParticlesSB.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, currParticlesSB.GetHandleUAV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(4, prevDrawParticlesArgsBB.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(5, currDrawParticlesArgsBB.GetHandleUAV()->HandleGPU);

	pCmdList->ExecuteIndirect(m_pUpdateParticlesCommandSig.Get(), 1, m_DispatchIndirectArgsBB.GetResource(), 0, nullptr, 0);

	DirectX::TransitionResource(pCmdList, prevParticlesSB.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void SWTessSampleApp::DrawParticles(ID3D12GraphicsCommandList* pCmdList, const StructuredBuffer& currParticlesSB, const ByteAddressBuffer& currDrawParticlesArgsBB)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw Particles");

	DirectX::TransitionResource(pCmdList, m_DrawParticlesTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DirectX::TransitionResource(pCmdList, currParticlesSB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	const DescriptorHandle* handleRTV = m_DrawParticlesTarget.GetHandleRTV();
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

	m_DrawParticlesTarget.ClearView(pCmdList);
	m_SceneDepthTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_DrawParticlesRootSig.GetPtr());
#ifndef DYNAMIC_RESOURCES 
	pCmdList->SetGraphicsRootDescriptorTable(0, m_CameraCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, currParticlesSB.GetHandleSRV()->HandleGPU);
#endif
	pCmdList->SetPipelineState(m_pDrawParticlesPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	pCmdList->ExecuteIndirect(m_pDrawParticlesCommandSig.Get(), 1, currDrawParticlesArgsBB.GetResource(), 0, nullptr, 0);

	DirectX::TransitionResource(pCmdList, m_DrawParticlesTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, currParticlesSB.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void SWTessSampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw BackBuffer");

	// R8_UNORMとR10G10B10A2_UNORMではCopyResourceでは非対応でエラーが出るのでシェーダでコピーする

	//DirectX::TransitionResource(pCmd, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	//pCmd->CopyResource(m_ColorTarget[m_FrameIndex].GetResource(), m_SSAO_FullResTarget.GetResource());
	//DirectX::TransitionResource(pCmd, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_BackBufferRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pBackBufferPSO.Get());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_BackBufferCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_DrawParticlesTarget.GetHandleSRV()->HandleGPU);

	// BackBufferのサイズはウィンドウサイズになっているのでアスペクト比を維持する
	DXGI_SWAP_CHAIN_DESC desc;
	m_pSwapChain->GetDesc(&desc);

	D3D12_VIEWPORT viewport = m_Viewport;
	if ((float)desc.BufferDesc.Width / desc.BufferDesc.Height < (float)m_Width / m_Height)
	{
		viewport.Width = (float)desc.BufferDesc.Width;
		viewport.Height = desc.BufferDesc.Width * ((float)m_Height / m_Width);
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = desc.BufferDesc.Height * 0.5f - viewport.Height * 0.5f;
	}
	else
	{
		viewport.Height = (float)desc.BufferDesc.Height;
		viewport.Width = desc.BufferDesc.Height * ((float)m_Width / m_Height);
		viewport.TopLeftX = desc.BufferDesc.Width * 0.5f - viewport.Width * 0.5f;
		viewport.TopLeftY = 0.0f;
	}

	pCmdList->RSSetViewports(1, &viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}

void SWTessSampleApp::DrawImGui(ID3D12GraphicsCommandList* pCmdList)
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

	ImGui::SliderInt("Num Spawn Per Frame", reinterpret_cast<int*>(& m_NumSpawnPerFrame), 1, 8192);
	ImGui::SliderInt("Initial Life", reinterpret_cast<int*>(& m_InitialLife), 1, 8192);
	ImGui::SliderFloat("Initial Velocity Scale", &m_InitialVelocityScale, 0.1f, 10.0f);

	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}
