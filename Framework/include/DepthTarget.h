#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class DepthTarget
{
public:
	DepthTarget();
	~DepthTarget();

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolDSV,
		DescriptorPool* pPoolSRV,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		float clearDepth,
		uint8_t clearStencil,
		LPCWSTR name = nullptr
	);

	void Term();

	DescriptorHandle* GetHandleDSV() const;
	DescriptorHandle* GetHandleSRV() const;
	ID3D12Resource* GetResource() const;
	D3D12_RESOURCE_DESC GetDesc() const;

	D3D12_DEPTH_STENCIL_VIEW_DESC GetDSVDesc() const;

	void ClearView(ID3D12GraphicsCommandList* pCmdList);

private:
	ComPtr<ID3D12Resource> m_pTarget;
	DescriptorHandle* m_pHandleDSV;
	DescriptorHandle* m_pHandleSRV;
	DescriptorPool* m_pPoolDSV;
	DescriptorPool* m_pPoolSRV;
	D3D12_DEPTH_STENCIL_VIEW_DESC m_DSVDesc;
	float m_ClearDepth;
	uint8_t m_ClearStencil;

	DepthTarget(const DepthTarget&) = delete;
	void operator=(const DepthTarget&) = delete;
};
