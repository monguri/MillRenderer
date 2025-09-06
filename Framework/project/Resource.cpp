#include "Resource.h"
#include "DescriptorPool.h"

Resource::Resource()
{
}

Resource::~Resource()
{
	Term();
}

bool Resource::Init
(
	ID3D12Device* pDevice,
	size_t size,
	D3D12_RESOURCE_DESC desc,
	D3D12_RESOURCE_FLAGS flags,
	D3D12_RESOURCE_STATES initState,
	DescriptorPool* pPoolSRV,
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc,
	DescriptorPool* pPoolUAV,
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc,
	ID3D12GraphicsCommandList* pCmdList,
	const void* pInitData
)
{
	if (pDevice == nullptr || size == 0)
	{
		return false;
	}

	// データ書き込みをする場合、コマンドリストが必要
	if (pInitData != nullptr && pCmdList == nullptr)
	{
		return false;
	}

	assert(m_pPoolSRV == nullptr);
	assert(m_pHandleSRV == nullptr);

	if (pPoolSRV != nullptr)
	{
		m_pPoolSRV = pPoolSRV;
		m_pPoolSRV->AddRef();

		m_pHandleSRV = pPoolSRV->AllocHandle();
		if (m_pHandleSRV == nullptr)
		{
			return false;
		}
	}

	assert(m_pPoolUAV == nullptr);
	assert(m_pHandleUAV == nullptr);

	if (pPoolUAV != nullptr)
	{
		m_pPoolUAV = pPoolUAV;
		m_pPoolUAV->AddRef();

		m_pHandleUAV = pPoolUAV->AllocHandle();
		if (m_pHandleUAV == nullptr)
		{
			return false;
		}
	}

	D3D12_HEAP_PROPERTIES prop = {};
	prop.Type = D3D12_HEAP_TYPE_DEFAULT;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initState,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

void Resource::Term()
{
	m_pResource.Reset();
	m_pUploadBuffer.Reset(); // TODO:本当はコールバックがあればコピーのExecuteCommandLists後にすぐ消せるのでだが。

	if (m_pHandleSRV != nullptr && m_pPoolSRV != nullptr)
	{
		m_pPoolSRV->FreeHandle(m_pHandleSRV);
		m_pHandleSRV = nullptr;
	}

	if (m_pPoolSRV != nullptr)
	{
		m_pPoolSRV->Release();
		m_pPoolSRV = nullptr;
	}

	if (m_pHandleUAV != nullptr && m_pPoolUAV != nullptr)
	{
		m_pPoolUAV->FreeHandle(m_pHandleUAV);
	}

	if (m_pPoolUAV != nullptr)
	{
		m_pPoolUAV->Release();
		m_pPoolUAV = nullptr;
	}

	m_pHandleSRV = nullptr;
	m_pHandleUAV = nullptr;
}

DescriptorHandle* Resource::GetHandleSRV() const
{
	return nullptr;
}

DescriptorHandle* Resource::GetHandleUAV() const
{
	return nullptr;
}

ID3D12Resource* Resource::GetResource() const
{
	return nullptr;
}
