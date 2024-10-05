#include "ParticleSampleApp.h"

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

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr float CAMERA_FOV_Y_DEGREE = 37.5f;
	static constexpr float CAMERA_NEAR = 0.1f;
	static constexpr float CAMERA_FAR = 100.0f;

	static constexpr uint32_t NUM_PARTICES = 10;

	struct alignas(256) CbCamera
	{
		Matrix ViewProj;
	};

	struct ParticleData
	{
		Vector3 Position;
		Vector3 Velocity;
		uint32_t Life;
	};

	struct alignas(256) CbTime
	{
		float DeltaTime;
		Vector3 Dummy;
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

ParticleSampleApp::ParticleSampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
{
}

ParticleSampleApp::~ParticleSampleApp()
{
}

bool ParticleSampleApp::OnInit(HWND hWnd)
{
	m_Camera.Reset();

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

	// パーティクル描画用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

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

    // パーティクル描画用ルートシグニチャとパイプラインステートの生成
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
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
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

		const Matrix& view = m_Camera.GetView();
		const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);
		const Matrix& viewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_CameraCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbCamera)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
			ptr->ViewProj = viewProj;
		}
	}

	// パーティクル用のStructuredBufferの作成
	{
		ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

		ParticleData particleData[NUM_PARTICES] = {
			{Vector3(0, 0, 0), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.1f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.2f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.3f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.4f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.5f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.6f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.7f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.8f), Vector3(0, 10, 0), 10},
			{Vector3(0, 0, 0.9f), Vector3(0, 10, 0), 10},
		};

		for (uint32_t i = 0; i < FRAME_COUNT; i++)
		{
			if (!m_ParticlesSB[i].Init<ParticleData>(m_pDevice.Get(), pCmd, m_pPool[POOL_TYPE_RES], m_pPool[POOL_TYPE_RES], NUM_PARTICES, true, particleData))
			{
				ELOG("Error : StructuredBuffer::Init() Failed.");
				return false;
			}
			DirectX::TransitionResource(pCmd, m_ParticlesSB[i].GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		pCmd->Close();

		ID3D12CommandList* pLists[] = {pCmd};
		m_pQueue->ExecuteCommandLists(1, pLists);

		// Wait command queue finishing.
		m_Fence.Wait(m_pQueue.Get(), INFINITE);
	}

	// 時間関係の定数バッファの作成
	{
		if (!m_TimeCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTime)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbTime* ptr = m_TimeCB.GetPtr<CbTime>();
		ptr->DeltaTime = 0.0f;
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

	m_PrevTime = std::chrono::high_resolution_clock::now();

	return true;
}

void ParticleSampleApp::OnTerm()
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
	}

	m_TimeCB.Term();
	m_BackBufferCB.Term();

	m_SceneDepthTarget.Term();
	m_DrawParticlesTarget.Term();

	m_pUpdateParticlesPSO.Reset();
	m_UpdateParticlesRootSig.Term();

	m_pDrawParticlesPSO.Reset();
	m_DrawParticlesRootSig.Term();

	m_pBackBufferPSO.Reset();
	m_BackBufferRootSig.Term();
}

void ParticleSampleApp::OnRender()
{
	using namespace std::chrono;
	const high_resolution_clock::time_point& currTime = high_resolution_clock::now();
	const milliseconds& elapsedMS = duration_cast<milliseconds>(currTime - m_PrevTime);
	m_PrevTime = currTime;

	const Matrix& view = m_Camera.GetView();
	constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
	float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
	const Matrix& viewProj = view * Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);

	// 定数バッファの更新
	{
		{
			CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
			ptr->ViewProj = viewProj;
		}

		{
			CbTime* ptr = m_TimeCB.GetPtr<CbTime>();
			ptr->DeltaTime = elapsedMS.count() / 1000.0f;
		}
	}

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);

	UpdateParticles(pCmd);
	DrawParticles(pCmd);

	DrawBackBuffer(pCmd);

	DrawImGui(pCmd);

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

bool ParticleSampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
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
					m_Camera.Reset();
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

		Camera::Event args = {};

		if (left)
		{
			args.Type = Camera::EventRotate;
			args.RotateH = DirectX::XMConvertToRadians(-0.5f * (x - m_PrevCursorX));
			args.RotateV = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_Camera.UpdateByEvent(args);
		}
		else if (right)
		{
			args.Type = Camera::EventDolly;
			args.Dolly = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_Camera.UpdateByEvent(args);
		}
		else if (middle)
		{
			args.Type = Camera::EventMove;
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
			m_Camera.UpdateByEvent(args);
		}

		m_PrevCursorX = x;
		m_PrevCursorY = y;
	}

	return true;
}

void ParticleSampleApp::UpdateParticles(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Update Particles");

	const StructuredBuffer& prevParticlesSB = m_ParticlesSB[m_FrameIndex];
	const StructuredBuffer& currParticlesSB = m_ParticlesSB[(m_FrameIndex + 1) % 2];

	DirectX::TransitionResource(pCmdList, prevParticlesSB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	pCmdList->SetComputeRootSignature(m_UpdateParticlesRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pUpdateParticlesPSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_TimeCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, prevParticlesSB.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, currParticlesSB.GetHandleUAV()->HandleGPU);

	// シェーダ側と合わせている
	const size_t NUM_THREAD_X = 64;

	// TODO: マジックナンバー
	UINT NumGroupX = DivideAndRoundUp(10, NUM_THREAD_X);
	pCmdList->Dispatch(NumGroupX, 1, 1);

	DirectX::TransitionResource(pCmdList, prevParticlesSB.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void ParticleSampleApp::DrawParticles(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw Particles");

	const StructuredBuffer& currParticlesSB = m_ParticlesSB[(m_FrameIndex + 1) % 2];

	DirectX::TransitionResource(pCmdList, m_DrawParticlesTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DirectX::TransitionResource(pCmdList, currParticlesSB.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	const DescriptorHandle* handleRTV = m_DrawParticlesTarget.GetHandleRTV();
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

	m_DrawParticlesTarget.ClearView(pCmdList);
	m_SceneDepthTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_DrawParticlesRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_CameraCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, currParticlesSB.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pDrawParticlesPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

	pCmdList->DrawInstanced(1, NUM_PARTICES, 0, 0);

	DirectX::TransitionResource(pCmdList, m_DrawParticlesTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, currParticlesSB.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void ParticleSampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
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

void ParticleSampleApp::DrawImGui(ID3D12GraphicsCommandList* pCmdList)
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

	ImGui::SeparatorText("Debug View");

	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}
