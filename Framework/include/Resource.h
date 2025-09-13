#pragma once

#include <d3d12.h>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class Resource
{
public:
	Resource();
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
		DescriptorPool* pPoolUAV,
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc
	);

#if 0
	template<typename T>
	bool InitAsConstantBuffer
	(
		ID3D12Device* pDevice,
		const T* pInitData
	)
	{
		return InitAsVertexBuffer
		(
			pDevice,
			sizeof(T),
			pInitData
		);
	}
#endif

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
	bool InitAsStructuredBuffer
	(
		ID3D12Device* pDevice,
		size_t count,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAV
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
			pPoolUAV
		);
	}

	bool InitAsByteAddressBuffer
	(
		ID3D12Device* pDevice,
		size_t size,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAV
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

	void* Map() const;
	void Unmap() const;

	template<typename T>
	T* Map() const
	{
		return reinterpret_cast<T*>(Map());
	}

	D3D12_VERTEX_BUFFER_VIEW GetVBV() const;
	DescriptorHandle* GetHandleSRV() const;
	DescriptorHandle* GetHandleUAV() const;
	ID3D12Resource* GetResource() const;

private:
	D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
	ComPtr<ID3D12Resource> m_pResource;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VBV;
	DescriptorHandle* m_pHandleSRV = nullptr;
	DescriptorHandle* m_pHandleUAV = nullptr;
	DescriptorPool* m_pPoolSRV = nullptr;
	DescriptorPool* m_pPoolUAV = nullptr;

#if 0
	bool InitAsConstantBuffer
	(
		ID3D12Device* pDevice,
		size_t size,
		const void* pInitData
	);
#endif

	bool InitAsVertexBuffer
	(
		ID3D12Device* pDevice,
		size_t stride,
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
		DescriptorPool* pPoolUAV
	);

	Resource(const Resource&) = delete;
	void operator=(const Resource&) = delete;
};
