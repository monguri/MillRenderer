#include "App.h"
#include "FileUtil.h"
#include <cassert>
#include <algorithm>

namespace
{
	const auto WindowClassName = TEXT("SampleWindowClass");

	inline int ComputeIntersectionArea(
		int ax1, int ay1,
		int ax2, int ay2,
		int bx1, int by1,
		int bx2, int by2
	)
	{
		return std::max(0, std::min(ax2, bx2) - std::max(ax1, bx1))
			* std::max(0, std::min(ay2, by2) - std::max(ay1, by1));
	}
}

App::App(uint32_t width, uint32_t height, DXGI_FORMAT format)
: m_hInst(nullptr)
, m_hWnd(nullptr)
, m_Width(width)
, m_Height(height)
, m_FrameIndex(0)
, m_BackBufferFormat(format)
{
}

App::~App()
{
}

void App::Run()
{
	if (InitApp())
	{
		MainLoop();
	}

	TermApp();
}

bool App::InitApp()
{
	if (!InitWnd())
	{
		return false;
	}

	if (!InitD3D())
	{
		return false;
	}

	if (!OnInit(m_hWnd))
	{
		return false;
	}

	ShowWindow(m_hWnd, SW_SHOWNORMAL);
	UpdateWindow(m_hWnd);
	SetFocus(m_hWnd);

	return true;
}

void App::TermApp()
{
	OnTerm();

	TermD3D();

	TermWnd();
}

bool App::InitWnd()
{
	// Get instance handle.
	HMODULE hInst = GetModuleHandle(nullptr);
	if (hInst == nullptr)
	{
		return false;
	}

	// Window class registration.
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hIcon = LoadIcon(hInst, IDI_APPLICATION);
	wc.hCursor = LoadCursor(hInst, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_BACKGROUND);
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = WindowClassName;
	wc.hIconSm = LoadIcon(hInst, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		return false;
	}

	m_hInst = hInst;

	// Create window.
	RECT rc = {};
	rc.right = static_cast<LONG>(m_Width);
	rc.bottom = static_cast<LONG>(m_Height);

	LONG style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&rc, style, FALSE);

	m_hWnd = CreateWindowEx
	(
		0,
		WindowClassName,
		TEXT("Sample"),
		style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		rc.right - rc.left,
		rc.bottom - rc.top,
		nullptr,
		nullptr,
		m_hInst,
		this
	);
	if (m_hWnd == nullptr)
	{
		return false;
	}

	return true;
}

void App::TermWnd()
{
	// Window class unregistration.
	if (m_hInst != nullptr)
	{
		UnregisterClass(WindowClassName, m_hInst);
	}

	m_hInst = nullptr;
	m_hWnd = nullptr;
}

bool App::InitD3D()
{
	// To debug d3d.
#if defined(DEBUG) || defined(_DEBUG)
	{
		ComPtr<ID3D12Debug1> pDebug;
		HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(pDebug.GetAddressOf()));
		if (SUCCEEDED(hr))
		{
			pDebug->EnableDebugLayer();

			hr = pDebug->QueryInterface(IID_PPV_ARGS(pDebug.GetAddressOf()));
			if (SUCCEEDED(hr))
			{
				// PSO検証を有効にする場合はここをtrueにする
				// ただし、PSO検証はかなり重い処理で起動が遅くなるので普段は無効にしておく
				pDebug->SetEnableGPUBasedValidation(false);
			}
		}
	}
#endif

	// Create dxgi factory.
	{
		HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(m_pFactory.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
	}

	ComPtr<IDXGIAdapter1> pAdapter = SelectAdapter();
	if (pAdapter == nullptr)
	{
		return false;
	}

#if defined(DEBUG) || defined(_DEBUG)
	{
		ComPtr<ID3D12InfoQueue> pInfoQueue;
		HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()));
		if (SUCCEEDED(hr))
		{
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		}
	}
#endif

	// Create command queue.
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;

		HRESULT hr = m_pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pQueue.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
	}

	// Create swap chain.
	{
		DXGI_SWAP_CHAIN_DESC desc = {};
		desc.BufferDesc.Width = m_Width;
		desc.BufferDesc.Height = m_Height;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		desc.BufferDesc.Format = m_BackBufferFormat;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = FRAME_COUNT;
		desc.OutputWindow = m_hWnd;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		ComPtr<IDXGISwapChain> pSwapChain = nullptr;
		HRESULT hr = m_pFactory->CreateSwapChain(m_pQueue.Get(), &desc, pSwapChain.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}

		hr = pSwapChain.As(&m_pSwapChain);
		if (FAILED(hr))
		{
			pSwapChain.Reset();
			return false;
		}

		m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

		pSwapChain.Reset();
	}

	// Create descriptor pool.
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};

		desc.NodeMask = 1;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 512;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (!DescriptorPool::Create(m_pDevice.Get(), &desc, &m_pPool[POOL_TYPE_RES_GPU_VISIBLE]))
		{
			return false;
		}

		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (!DescriptorPool::Create(m_pDevice.Get(), &desc, &m_pPool[POOL_TYPE_RES_CPU_VISIBLE]))
		{
			return false;
		}

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		desc.NumDescriptors = 256;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (!DescriptorPool::Create(m_pDevice.Get(), &desc, &m_pPool[POOL_TYPE_SMP]))
		{
			return false;
		}

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = 512;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (!DescriptorPool::Create(m_pDevice.Get(), &desc, &m_pPool[POOL_TYPE_RTV]))
		{
			return false;
		}

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		desc.NumDescriptors = 512;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (!DescriptorPool::Create(m_pDevice.Get(), &desc, &m_pPool[POOL_TYPE_DSV]))
		{
			return false;
		}
	}

	// Create command list.
	{
		if (!m_CommandList.Init(m_pDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, FRAME_COUNT))
		{
			return false;
		}
	}

	// Create render target view.
	{
		for (uint32_t i = 0u; i < FRAME_COUNT; ++i)
		{
			if (!m_ColorTarget[i].InitFromBackBuffer(m_pDevice.Get(), m_pPool[POOL_TYPE_RTV], true, i, m_pSwapChain.Get()))
			{
				return false;
			}
		}
	}

	// Create fence.
	{
		if (!m_Fence.Init(m_pDevice.Get()))
		{
			return false;
		}
	}

	// Viewport settings.
	{
		m_Viewport.TopLeftX = 0;
		m_Viewport.TopLeftY = 0;
		m_Viewport.Width = (FLOAT)m_Width;
		m_Viewport.Height = (FLOAT)m_Height;
		m_Viewport.MinDepth = 0.0f;
		m_Viewport.MaxDepth = 1.0f;
	}

	// Scissor rect settings.
	{
		m_Scissor.left = 0;
		m_Scissor.right = (LONG)m_Width;
		m_Scissor.top = 0;
		m_Scissor.bottom = (LONG)m_Height;
	}

	// Initialize COM to use WIC.
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

void App::TermD3D()
{
	// Wait command queue finishing.
	m_Fence.Sync(m_pQueue.Get());

	m_Fence.Term();

	for (uint32_t i = 0u; i < FRAME_COUNT; ++i)
	{
		m_ColorTarget[i].Term();
	}

	m_CommandList.Term();

	for (size_t i = 0; i < POOL_COUNT; ++i)
	{
		if (m_pPool[i] != nullptr)
		{
			m_pPool[i]->Release();
			m_pPool[i] = nullptr;
		}
	}

	m_pSwapChain.Reset();

	m_pFactory.Reset();

	m_pQueue.Reset();

	m_pDevice.Reset();

    CoUninitialize();
}

void App::MainLoop()
{
	MSG msg = {};

	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) == TRUE)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			OnRender();
		}
	}
}

ComPtr<IDXGIAdapter1> App::SelectAdapter()
{
	// Select an adapter which is HW and supports ray tracing.
	ComPtr<IDXGIAdapter1> pAdapter;
	bool foundAdapter = false;
	for (uint32_t i = 0; !foundAdapter && m_pFactory->EnumAdapters1(i, pAdapter.GetAddressOf()) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		// Create device. 12.0 is needed for ray tracing.
		HRESULT hr = D3D12CreateDevice(
			pAdapter.Get(),
			D3D_FEATURE_LEVEL_12_0,
			IID_PPV_ARGS(m_pDevice.GetAddressOf())
		);
		if (FAILED(hr))
		{
			continue;
		}

		// Check feature support. D3D12_OPTIONS5 is needed for ray tracing.
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5 = {};
		hr = m_pDevice->CheckFeatureSupport(
			D3D12_FEATURE_D3D12_OPTIONS5,
			&features5,
			sizeof(features5)
		);
		if (FAILED(hr))
		{
			continue;
		}
		if (features5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			continue;
		}

		// Check feature support. D3D12_OPTIONS7 is needed for mesh shader.
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 features7 = {};
		hr = m_pDevice->CheckFeatureSupport(
			D3D12_FEATURE_D3D12_OPTIONS7,
			&features7,
			sizeof(features7)
		);
		if (FAILED(hr))
		{
			continue;
		}
		if (features7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
		{
			continue;
		}

		// Check feature support. D3D_SHADER_MODEL_6_5 is needed for mesh shader.
		// D3D_SHADER_MODEL_6_6 is needed for dynamic resources.
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel;
		shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
		hr = m_pDevice->CheckFeatureSupport(
			D3D12_FEATURE_SHADER_MODEL,
			&shaderModel,
			sizeof(shaderModel)
		);
		if (FAILED(hr))
		{
			continue;
		}
		if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6)
		{
			continue;
		}

		foundAdapter = true;
	}

	if (foundAdapter)
	{
		return pAdapter;
	}
	else
	{
		return nullptr;
	}
}

void App::Present(uint32_t interval)
{
	m_pSwapChain->Present(interval, 0);

	// Wait command queue finishing.
	m_Fence.Wait(m_pQueue.Get(), INFINITE);

	m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
}

bool App::IsSupportHDR() const
{
	return m_SupportHDR;
}

float App::GetMaxLuminance() const
{
	return m_MaxLuminance;
}

float App::GetMinLuminance() const
{
	return m_MinLuminance;
}

bool App::CheckSupportHDR()
{
	if (m_pSwapChain == nullptr || m_pFactory == nullptr || m_pDevice == nullptr)
	{
		return false;
	}

	HRESULT hr = S_OK;

	if (!m_pFactory->IsCurrent())
	{
		m_pFactory.Reset();

		hr = CreateDXGIFactory2(0, IID_PPV_ARGS(m_pFactory.GetAddressOf()));
		if (FAILED(hr))
		{
			return false;
		}
	}


	RECT rect;
	GetWindowRect(m_hWnd, &rect);

	UINT i = 0;
	ComPtr<IDXGIOutput> currentOutput;
	ComPtr<IDXGIOutput> bestOutput;
	int bestIntersectArea = -1;

	ComPtr<IDXGIAdapter1> pAdapter = SelectAdapter();
	if (pAdapter == nullptr)
	{
		return false;
	}

	// Find best intersected display to this application window.
	for (UINT i = 0; pAdapter->EnumOutputs(i, &currentOutput) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_OUTPUT_DESC desc;
		hr = currentOutput->GetDesc(&desc);
		if (FAILED(hr))
		{
			return false;
		}

		int intersectArea = ComputeIntersectionArea(
			rect.left, rect.top,
			rect.right, rect.bottom,
			desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
			desc.DesktopCoordinates.right, desc.DesktopCoordinates.bottom
		);
		if (intersectArea > bestIntersectArea)
		{
			bestOutput = currentOutput;
			bestIntersectArea = intersectArea;
		}
	}

	// Check HDR and luminance information from the display.
	ComPtr<IDXGIOutput6> pOutput6;
	hr = bestOutput.As(&pOutput6);
	if (FAILED(hr))
	{
		return false;
	}

	DXGI_OUTPUT_DESC1 desc1;
	hr = pOutput6->GetDesc1(&desc1);
	if (FAILED(hr))
	{
		return false;
	}

	m_SupportHDR = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
	m_MaxLuminance = desc1.MaxLuminance;
	m_MinLuminance = desc1.MinLuminance;
	return true;
}

LRESULT CALLBACK App::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	App* instance = reinterpret_cast<App*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (msg)
	{
		case WM_CREATE:
		{
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lp);
			LONG_PTR pApp = reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, pApp);
		}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_SIZE:
			if (instance != nullptr
				&& instance->m_pDevice != nullptr
				&& instance->m_pSwapChain != nullptr
				&& instance->m_pPool != nullptr
				&& wp != SIZE_MINIMIZED)
			{
				// バックバッファのレンダーターゲットを作り直すのでコマンドリスト終了まで待つ
				instance->m_Fence.Sync(instance->m_pQueue.Get());

				for (uint32_t i = 0u; i < FRAME_COUNT; ++i)
				{
					instance->m_ColorTarget[i].Term();
				}

				// Format引数とFlags引数は初期化時に作ったDXGI_SWAP_CHAIN_DESCと同じ値にする
				HRESULT hr = instance->m_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
				if (FAILED(hr))
				{
					return false;
				}

				// Re-Create render target view.
				for (uint32_t i = 0u; i < FRAME_COUNT; ++i)
				{
					if (!instance->m_ColorTarget[i].InitFromBackBuffer(instance->m_pDevice.Get(), instance->m_pPool[POOL_TYPE_RTV], true, i, instance->m_pSwapChain.Get()))
					{
						return false;
					}
				}

				instance->m_FrameIndex = instance->m_pSwapChain->GetCurrentBackBufferIndex();
			}
			break;
		case WM_MOVE:
		case WM_DISPLAYCHANGE:
			if (instance != nullptr)
			{
				if (!instance->CheckSupportHDR())
				{
					return false;
				}
			}
			break;
		default:
			break;
	}

	if (instance != nullptr)
	{
		if (!instance->OnMsgProc(hWnd, msg, wp, lp))
		{
			return true;
		}
	}

	return DefWindowProc(hWnd, msg, wp, lp);
}
