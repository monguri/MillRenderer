#pragma once

#include <d3d12.h>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class ConstantBuffer
{
public:
	ConstantBuffer();
	~ConstantBuffer();

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPool,
		size_t size
	);

	void Term();

	D3D12_GPU_VIRTUAL_ADDRESS GetAddress() const;

	D3D12_CPU_DESCRIPTOR_HANDLE GetHandleCPU() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU() const;

	void* GetPtr() const;

	template<typename T>
	T* GetPtr() const
	{
		return reinterpret_cast<T*>(GetPtr());
	}

private:
	ComPtr<ID3D12Resource> m_pCB;
	DescriptorHandle* m_pHandle;
	DescriptorPool* m_pPool;
	D3D12_CONSTANT_BUFFER_VIEW_DESC m_Desc;
	void* m_pMappedPtr;

	ConstantBuffer(const ConstantBuffer&) = delete;
	void operator=(const ConstantBuffer&) = delete;
};
