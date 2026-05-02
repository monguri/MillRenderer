#pragma once

#include <d3d12.h>
#include <cstdint>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class Resource
{
public:
	virtual ~Resource();

	bool Init
	(
		ID3D12Device* pDevice,
		size_t size,
		D3D12_HEAP_PROPERTIES heapProp,
		D3D12_RESOURCE_DESC desc,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc,
		DescriptorPool* pPoolUAVGpuVisible,
		DescriptorPool* pPoolUAVCpuVisible,
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc,
		LPCWSTR name = nullptr
	);

	template<typename T>
	bool InitAsConstantBuffer
	(
		ID3D12Device* pDevice,
		D3D12_HEAP_TYPE heapType,
		DescriptorPool* pPoolCBV,
		LPCWSTR name = nullptr
	)
	{
		return InitAsConstantBuffer
		(
			pDevice,
			sizeof(T),
			heapType,
			pPoolCBV,
			name
		);
	}

	bool InitAsConstantBuffer
	(
		ID3D12Device* pDevice,
		size_t size,
		D3D12_HEAP_TYPE heapType,
		DescriptorPool* pPoolCBV,
		LPCWSTR name = nullptr
	);

	template<typename T>
	bool InitAsVertexBuffer
	(
		ID3D12Device* pDevice,
		size_t count
	)
	{
		return InitAsVertexBuffer
		(
			pDevice,
			sizeof(T),
			count * sizeof(T)
		);
	}

	template<typename T>
	bool InitAsIndexBuffer
	(
		ID3D12Device* pDevice,
		DXGI_FORMAT format,
		size_t count
	)
	{
		return InitAsIndexBuffer
		(
			pDevice,
			format,
			count * sizeof(T)
		);
	}

	// StructuredBufferÇÕí èÌÇ≈ÇÕClearUnorderedAccessViewUint()ÇÕégÇ¶Ç»Ç¢ÇΩÇﬂ
	// CpuVisibleÇ»DescriptorPoolÇ≈UAVÇçÏÇÈà”ñ°Ç™Ç»Ç¢
	template<typename T>
	bool InitAsStructuredBuffer
	(
		ID3D12Device* pDevice,
		size_t count,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAVGpuVisible,
		LPCWSTR name = nullptr
	)
	{
		return InitAsStructuredBuffer
		(
			pDevice,
			count,
			sizeof(T),
			flags,
			state,
			pPoolSRV,
			pPoolUAVGpuVisible,
			name
		);
	}

	bool InitAsByteAddressBuffer
	(
		ID3D12Device* pDevice,
		size_t size,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAVGpuVisible,
		DescriptorPool* pPoolUAVCpuVisible,
		LPCWSTR name = nullptr
	);

	void Term();

	template<typename T>
	bool UploadBufferTypeData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		size_t count,
		const T* pData
	)
	{
		return UploadBufferData
		(
			pDevice,
			pCmdList,
			sizeof(T) * count,
			pData
		);
	}

	bool UploadBufferData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		size_t size,
		const void* pData
	);

	template<typename T>
	T* Map() const
	{
		return reinterpret_cast<T*>(Map());
	}

	void Unmap() const;

	void ClearUavWithUintValue(ID3D12GraphicsCommandList* pCmdList, uint32_t value[4]) const;

	void BarrierUAV(ID3D12GraphicsCommandList* pCmdList) const;

	D3D12_VERTEX_BUFFER_VIEW GetVBV() const;
	D3D12_INDEX_BUFFER_VIEW GetIBV() const;
	DescriptorHandle* GetHandleCBV() const;
	DescriptorHandle* GetHandleSRV() const;
	DescriptorHandle* GetHandleUAV() const;
	ID3D12Resource* GetResource() const;

private:
	D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
	ComPtr<ID3D12Resource> m_pResource;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VBV;
	D3D12_INDEX_BUFFER_VIEW m_IBV;
	DescriptorHandle* m_pHandleSRV = nullptr;
	DescriptorHandle* m_pHandleUAVGpuVisible = nullptr;
	DescriptorHandle* m_pHandleUAVCpuVisible = nullptr;
	DescriptorPool* m_pPoolSRV = nullptr;
	DescriptorPool* m_pPoolUAVGpuVisible = nullptr;
	DescriptorPool* m_pPoolUAVCpuVisible = nullptr;

	bool InitAsVertexBuffer
	(
		ID3D12Device* pDevice,
		size_t stride,
		size_t size
	);

	bool InitAsIndexBuffer
	(
		ID3D12Device* pDevice,
		DXGI_FORMAT format,
		size_t size
	);

	bool InitAsStructuredBuffer
	(
		ID3D12Device* pDevice,
		size_t count,
		size_t structureSize,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAVGpuVisible,
		LPCWSTR name = nullptr
	);

	void* Map() const;

	void operator=(const Resource&) = delete;
};
