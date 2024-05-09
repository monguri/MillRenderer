#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <vector>
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
		float clearColor[4],
		uint32_t mipLevels = 1
	);

	bool InitUnorderedAccessTarget
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolUAV,
		DescriptorPool* pPoolRTV,
		DescriptorPool* pPoolSRV,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		float clearColor[4],
		uint32_t mipLevels = 1,
		uint32_t depth = 1
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
	const std::vector<DescriptorHandle*>& GetHandleUAVs() const;
	DescriptorHandle* GetHandleSRV() const;

	ID3D12Resource* GetResource() const;
	D3D12_RESOURCE_DESC GetDesc() const;

	D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc() const;
	const std::vector<D3D12_UNORDERED_ACCESS_VIEW_DESC>& GetUAVDescs() const;
	D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc() const;

	void ClearView(ID3D12GraphicsCommandList* pCmdList);

private:
	ComPtr<ID3D12Resource> m_pTarget;
	DescriptorHandle* m_pHandleRTV;
	std::vector<DescriptorHandle*> m_pHandleMipUAVs;
	DescriptorHandle* m_pHandleSRV;
	DescriptorPool* m_pPoolRTV;
	DescriptorPool* m_pPoolUAV;
	DescriptorPool* m_pPoolSRV;
	D3D12_RENDER_TARGET_VIEW_DESC m_RTVDesc;
	std::vector<D3D12_UNORDERED_ACCESS_VIEW_DESC> m_MipUAVDescs;
	D3D12_SHADER_RESOURCE_VIEW_DESC m_SRVDesc;
	float m_ClearColor[4];

	ColorTarget(const ColorTarget&) = delete;
	void operator=(const ColorTarget&) = delete;
};

