#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class ColorTarget
{
public:
	ColorTarget();
	~ColorTarget();

	bool InitRenderTarget
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolRTV,
		DescriptorPool* pPoolSRV,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		float clearColor[4]
	);

	bool InitFromBackBuffer
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolRTV,
		bool useSRGB,
		uint32_t index,
		IDXGISwapChain* pSwapChain
	);

	bool InitFromData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPoolSRV,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		size_t pixelSize,
		const void* pInitData
	);

	template<typename T>
	bool InitFromData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPoolSRV,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		const T* pInitData
	)
	{
		return InitFromData(
			pDevice,
			pCmdList,
			pPoolSRV,
			width,
			height,
			format,
			sizeof(T),
			pInitData
		);
	}

	void Term();

	DescriptorHandle* GetHandleRTV() const;
	DescriptorHandle* GetHandleSRV() const;

	ID3D12Resource* GetResource() const;
	D3D12_RESOURCE_DESC GetDesc() const;

	D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc() const;
	D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc() const;

	void ClearView(ID3D12GraphicsCommandList* pCmdList);

private:
	ComPtr<ID3D12Resource> m_pTarget;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	DescriptorHandle* m_pHandleRTV;
	DescriptorHandle* m_pHandleSRV;
	DescriptorPool* m_pPoolRTV;
	DescriptorPool* m_pPoolSRV;
	D3D12_RENDER_TARGET_VIEW_DESC m_RTVDesc;
	D3D12_SHADER_RESOURCE_VIEW_DESC m_SRVDesc;
	float m_ClearColor[4];

	ColorTarget(const ColorTarget&) = delete;
	void operator=(const ColorTarget&) = delete;
};

