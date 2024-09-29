#pragma once

#include <d3d12.h>
#include "ComPtr.h"

class DescriptorHandle;
class DescriptorPool;

class StructuredBuffer
{
public:
	StructuredBuffer();
	~StructuredBuffer();

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAV,
		size_t count,
		size_t structureSize,
		bool useUAV,
		const void* pInitData = nullptr
	);

	template<typename T>
	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPoolSRV,
		DescriptorPool* pPoolUAV,
		size_t count,
		bool useUAV,
		const T* pInitData
	)
	{
		return Init(pDevice, pCmdList, pPoolSRV, pPoolUAV, count, sizeof(T), useUAV, pInitData);
	}

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

	StructuredBuffer(const StructuredBuffer&) = delete;
	void operator=(const StructuredBuffer&) = delete;
};
