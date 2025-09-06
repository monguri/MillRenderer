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
		D3D12_RESOURCE_DESC desc,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES state,
		DescriptorPool* pPoolSRV,
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc,
		DescriptorPool* pPoolUAV,
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc
	);

	void Term();

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

	DescriptorHandle* GetHandleSRV() const;
	DescriptorHandle* GetHandleUAV() const;
	ID3D12Resource* GetResource() const;

private:
	D3D12_RESOURCE_STATES m_state;
	ComPtr<ID3D12Resource> m_pResource;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	DescriptorHandle* m_pHandleSRV = nullptr;
	DescriptorHandle* m_pHandleUAV = nullptr;
	DescriptorPool* m_pPoolSRV = nullptr;
	DescriptorPool* m_pPoolUAV = nullptr;

	Resource(const Resource&) = delete;
	void operator=(const Resource&) = delete;
};

