#pragma once

#include <Windows.h>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "ComPtr.h"
#include "DescriptorPool.h"
#include "ColorTarget.h"
#include "DepthTarget.h"
#include "CommandList.h"
#include "Fence.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment(lib, "assimp-vc142-mtd.lib")
#else
#pragma comment(lib, "assimp-vc142-mt.lib")
#endif

class App
{
public:
	// Number of frame buffers
	static const uint32_t FRAME_COUNT = 2;

	App(uint32_t width, uint32_t height, DXGI_FORMAT format);
	~App();
	void Run();

protected:
	enum POOL_TYPE
	{
		POOL_TYPE_RES_GPU_VISIBLE, // CBV/SRV/UAV
		POOL_TYPE_RES_CPU_VISIBLE, // CBV/SRV/UAV
		POOL_TYPE_SMP, // Sampler
		POOL_TYPE_RTV, // RTV
		POOL_TYPE_DSV, // DSV
		POOL_COUNT,
	};

	// Window
	HINSTANCE m_hInst;
	HWND m_hWnd;
	uint32_t m_Width;
	uint32_t m_Height;

	ComPtr<IDXGIFactory4> m_pFactory;
	ComPtr<ID3D12Device5> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pQueue;
	ComPtr<IDXGISwapChain4> m_pSwapChain;
	DescriptorPool* m_pPool[POOL_COUNT];
	ColorTarget m_ColorTarget[FRAME_COUNT];
	CommandList m_CommandList;
	Fence m_Fence;
	uint32_t m_FrameIndex;
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_Scissor;
	DXGI_FORMAT m_BackBufferFormat;

	ComPtr<IDXGIAdapter1> SelectAdapter();
	void Present(uint32_t interval);
	bool IsSupportHDR() const;
	float GetMaxLuminance() const;
	float GetMinLuminance() const;

	virtual bool OnInit(HWND hWnd) { return true; }
	virtual void OnTerm() {}
	virtual void OnRender() {}
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) { return true; }

private:
	bool m_SupportHDR;
	// Display luminance
	float m_MaxLuminance;
	float m_MinLuminance;

	bool InitApp();
	void TermApp();
	bool InitWnd();
	void TermWnd();
	bool InitD3D();
	void TermD3D();
	void MainLoop();
	bool CheckSupportHDR();

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
};
