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

	// Software TessellationによるVB/IB作成用ルートシグニチャとパイプラインステートの生成
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

    // バックバッファ描画用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"ViewProjVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"WireframePS.cso", psPath))
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout.NumElements = 0;
		desc.InputLayout.pInputElementDescs = nullptr;
		desc.pRootSignature = m_BackBufferRootSig.GetPtr();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;
		desc.DSVFormat = m_SceneDepthTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();

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

	pCmd->Close();
	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);
	// Wait command queue finishing.
	m_Fence.Wait(m_pQueue.Get(), INFINITE);

	return true;
}

void SWTessSampleApp::OnTerm()
{
	// imgui終了処理
	if (ImGui::GetCurrentContext() != nullptr)
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	m_SWTessResultIB.Term();
	m_SWTessResultIB.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_CameraCB[i].Term();
	}

	m_SceneDepthTarget.Term();

	m_pUpdateParticlesPSO.Reset();
	m_UpdateParticlesRootSig.Term();

	m_pBackBufferPSO.Reset();
	m_BackBufferRootSig.Term();
}

void SWTessSampleApp::OnRender()
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

	TessellateQuad(pCmd);

	DrawBackBuffer(pCmd);

	DrawImGui(pCmd);

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

bool SWTessSampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
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

void SWTessSampleApp::TessellateQuad(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Update Particles");

	// 定数バッファの更新
	{
	}
}

void SWTessSampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Draw BackBuffer");

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);
	m_SceneDepthTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_BackBufferRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pBackBufferPSO.Get());

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
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_SWTessResultVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_SWTessResultIB.GetView();
	pCmdList->IASetIndexBuffer(&IBV);

	pCmdList->DrawIndexedInstanced(m_SWTessResultIB.GetCount(), 1, 0, 0, 0);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
