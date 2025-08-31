#pragma once

#include <d3d12.h>
#include "ComPtr.h"

class DescriptorHandle;
class DescriptorPool;

class ByteAddressBuffer
{
public:
	ByteAddressBuffer();
	~ByteAddressBuffer();

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAV,
		size_t count,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initState,
		const void* pInitData
	);

	void Term();

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
	ComPtr<ID3D12Resource> m_pBuffer;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	DescriptorHandle* m_pHandleSRV;
	DescriptorHandle* m_pHandleUAV;
	DescriptorPool* m_pPoolUAV;
	DescriptorPool* m_pPoolSRV;

	ByteAddressBuffer(const ByteAddressBuffer&) = delete;
	void operator=(const ByteAddressBuffer&) = delete;
};
