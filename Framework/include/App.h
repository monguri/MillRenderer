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

class App
{
public:
	App(uint32_t width, uint32_t height, DXGI_FORMAT format);
	~App();
	void Run();

protected:
	enum POOL_TYPE
	{
		POOL_TYPE_RES = 0, // CBV/SRV/UAV
		POOL_TYPE_SMP = 1, // Sampler
		POOL_TYPE_RTV = 2, // RTV
		POOL_TYPE_DSV = 3, // DSV
		POOL_COUNT = 4,
	};

	static const uint32_t FrameCount = 2;

	HINSTANCE m_hInst;
	HWND m_hWnd;
	uint32_t m_Width;
	uint32_t m_Height;

	ComPtr<IDXGIFactory4> m_pFactory;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pQueue;
	ComPtr<IDXGISwapChain4> m_pSwapChain;
	DescriptorPool* m_pPool[POOL_COUNT];
	ColorTarget m_ColorTarget[FrameCount];
	DepthTarget m_DepthTarget;
	CommandList m_CommandList;
	Fence m_Fence;
	uint32_t m_FrameIndex;
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_Scissor;
	DXGI_FORMAT m_BackBufferFormat;

	void Present(uint32_t interval);
	bool IsSupportHDR() const;
	float GetMaxLuminance() const;
	float GetMinLuminance() const;

	virtual bool OnInit() { return true; }
	virtual void OnTerm() {}
	virtual void OnRender() {}
	virtual void OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {}

private:
	bool m_supportHDR;
	float m_MaxLuminance;
	float m_MinLuminance;

	bool InitApp();
	void TermApp();
	bool InitWnd();
	void TermWnd();
	bool InitD3D();
	void TermD3D();
	void MainLoop();
	void CheckSupportHDR();

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
};
