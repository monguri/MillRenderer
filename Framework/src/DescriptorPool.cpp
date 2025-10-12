#include "DescriptorPool.h"

DescriptorPool::DescriptorPool()
: m_RefCount(1)
, m_Pool()
, m_pHeap()
, m_DescriptorSize(0)
{
}

DescriptorPool::~DescriptorPool()
{
	m_Pool.Term();
	m_pHeap.Reset();
	m_DescriptorSize = 0;
}

bool DescriptorPool::Create(
	ID3D12Device* pDevice,
	const D3D12_DESCRIPTOR_HEAP_DESC* pDesc,
	DescriptorPool** ppPool
)
{
	if (pDevice == nullptr || pDesc == nullptr || ppPool == nullptr)
	{
		return false;
	}

	DescriptorPool* instance = new (std::nothrow) DescriptorPool();
	if (instance == nullptr)
	{
		return false;
	}

	HRESULT hr = pDevice->CreateDescriptorHeap(
		pDesc,
		IID_PPV_ARGS(instance->m_pHeap.GetAddressOf())
	);
	if (FAILED(hr))
	{
		instance->Release();
		return false;
	}

	if (!instance->m_Pool.Init(pDesc->NumDescriptors))
	{
		instance->Release();
		return false;
	}

	instance->m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(pDesc->Type);

	*ppPool = instance;
	return true;
}

void DescriptorPool::AddRef()
{
	m_RefCount++;
}

void DescriptorPool::Release()
{
	m_RefCount--;
	if (m_RefCount == 0)
	{
		delete this;
	}
}

uint32_t DescriptorPool::GetCount() const
{
	return m_RefCount;
}

DescriptorHandle* DescriptorPool::AllocHandle()
{
	const auto& func = [&](uint32_t index, DescriptorHandle* pHandle)
	{
		pHandle->m_IndexInDescriptorHeap = index;

		D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = m_pHeap->GetCPUDescriptorHandleForHeapStart();
		handleCPU.ptr += m_DescriptorSize * index;
		
		D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = m_pHeap->GetGPUDescriptorHandleForHeapStart();
		handleGPU.ptr += m_DescriptorSize * index;

		pHandle->HandleCPU = handleCPU;
		pHandle->HandleGPU = handleGPU;
	};

	return m_Pool.Alloc(func);
}

void DescriptorPool::FreeHandle(DescriptorHandle*& pHandle)
{
	if (pHandle != nullptr)
	{
		m_Pool.Free(pHandle);
		pHandle = nullptr;
	}
}

uint32_t DescriptorPool::GetAvailableHandleCount() const
{
	return m_Pool.GetAvailableCount();
}

uint32_t DescriptorPool::GetAllocatedHandleCount() const
{
	return m_Pool.GetUsedCount();
}

uint32_t DescriptorPool::GetHandleCount() const
{
	return m_Pool.GetSize();
}

ID3D12DescriptorHeap* const DescriptorPool::GetHeap() const
{
	return m_pHeap.Get();
}
