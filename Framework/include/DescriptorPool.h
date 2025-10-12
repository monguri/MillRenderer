#pragma once

#include <d3d12.h>
#include <atomic>
#include "ComPtr.h"
#include "Pool.h"

class DescriptorHandle
{
public:
	// DescriptorPool.m_DescriptorSizeがわかっていればm_IndexInDescriptorHeapからHandleCPU/HandleGPUは計算できるし、
	// 逆もしかりなのでメモリ的には無駄だが、利便性のために持っておく。
	uint32_t m_IndexInDescriptorHeap = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE HandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE HandleGPU;

	uint32_t GetIndexInDescriptorHeap() const
	{
		return m_IndexInDescriptorHeap;
	}

	bool HasCPU() const
	{
		return HandleCPU.ptr != 0;
	}

	bool HasGPU() const
	{
		return HandleGPU.ptr != 0;
	}
};

class DescriptorPool
{
public:
	static bool Create(
		ID3D12Device* pDevice,
		const D3D12_DESCRIPTOR_HEAP_DESC* pDesc,
		DescriptorPool** ppPool
	);

	void AddRef();
	void Release();
	uint32_t GetCount() const;

	DescriptorHandle* AllocHandle();
	void FreeHandle(DescriptorHandle*& pHandle);

	uint32_t GetAvailableHandleCount() const;
	uint32_t GetAllocatedHandleCount() const;
	uint32_t GetHandleCount() const;
	ID3D12DescriptorHeap* const GetHeap() const;

private:
	std::atomic<uint32_t> m_RefCount;
	Pool<DescriptorHandle> m_Pool;
	ComPtr<ID3D12DescriptorHeap> m_pHeap;
	uint32_t m_DescriptorSize;

	DescriptorPool();
	~DescriptorPool();

	DescriptorPool(const DescriptorPool&) = delete;
	void operator=(const DescriptorPool&) = delete;
};
