#include "Resource.h"
#include "DescriptorPool.h"
#include <DirectXHelpers.h>

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
	D3D12_RESOURCE_STATES state,
	DescriptorPool* pPoolSRV,
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc,
	DescriptorPool* pPoolUAV,
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc
)
{
	if (pDevice == nullptr || size == 0)
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

	m_state = state;

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		state,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	if (m_pHandleSRV != nullptr)
	{
		pDevice->CreateShaderResourceView(
			m_pResource.Get(),
			&srvDesc,
			m_pHandleSRV->HandleCPU
		);
	}

	if (m_pHandleUAV != nullptr)
	{
		pDevice->CreateUnorderedAccessView(
			m_pResource.Get(),
			nullptr,
			&uavDesc,
			m_pHandleUAV->HandleCPU
		);
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

bool Resource::UploadBufferData
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	size_t size,
	const void* pData
)
{
	if (pDevice == nullptr || pCmdList == nullptr || size == 0 || pData == nullptr)
	{
		return false;
	}

	DirectX::TransitionResource(pCmdList, m_pResource.Get(), m_state, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_HEAP_PROPERTIES prop = {};
	prop.Type = D3D12_HEAP_TYPE_UPLOAD;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_pUploadBuffer.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	void* ptr;
	hr = m_pUploadBuffer->Map(0, nullptr, &ptr);
	if (FAILED(hr) || ptr == nullptr)
	{
		return false;
	}

	memcpy(ptr, pData, size);

	m_pUploadBuffer->Unmap(0, nullptr);

	pCmdList->CopyBufferRegion(m_pResource.Get(), 0, m_pUploadBuffer.Get(), 0, size);

	DirectX::TransitionResource(pCmdList, m_pResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, m_state);

	return true;
}

void* Resource::Map() const
{
	void* ptr;
	HRESULT hr = m_pResource->Map(0, nullptr, &ptr);
	if (FAILED(hr))
	{
		return nullptr;
	}

	return ptr;
}

void Resource::Unmap() const
{
	m_pResource->Unmap(0, nullptr);
}

DescriptorHandle* Resource::GetHandleSRV() const
{
	return m_pHandleSRV;
}

DescriptorHandle* Resource::GetHandleUAV() const
{
	return m_pHandleUAV;
}

ID3D12Resource* Resource::GetResource() const
{
	return m_pResource.Get();
}
