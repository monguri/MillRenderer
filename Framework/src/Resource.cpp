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
	D3D12_HEAP_PROPERTIES heapProp,
	D3D12_RESOURCE_DESC desc,
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

	m_state = state;

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&heapProp,
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

bool Resource::InitAsConstantBuffer
(
	ID3D12Device* pDevice,
	size_t size,
	D3D12_HEAP_TYPE heapType,
	DescriptorPool* pPoolCBV
)
{
	if (pDevice == nullptr || pPoolCBV == nullptr)
	{
		return false;
	}

	// m_pPoolSRV, m_pHandleSRVをCBV用に使う。SRVとCBVを同人に使うことがないので。

	assert(m_pPoolSRV == nullptr);
	assert(m_pHandleSRV == nullptr);

	m_pPoolSRV = pPoolCBV;
	m_pPoolSRV->AddRef();

	m_pHandleSRV = m_pPoolSRV->AllocHandle();
	if (m_pHandleSRV == nullptr)
	{
		return false;
	}

	size_t align = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	UINT64 sizeAligned = (size + (align - 1)) & ~(align - 1);

	D3D12_HEAP_PROPERTIES prop = {};
	prop.Type = heapType;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = sizeAligned;
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
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}
	
	D3D12_CONSTANT_BUFFER_VIEW_DESC descCBV = {};
	descCBV.BufferLocation = m_pResource->GetGPUVirtualAddress();
	descCBV.SizeInBytes = static_cast<UINT>(sizeAligned);
	pDevice->CreateConstantBufferView(&descCBV, m_pHandleSRV->HandleCPU);

	return true;
}

bool Resource::InitAsVertexBuffer(ID3D12Device* pDevice, size_t stride, size_t size)
{
	if (pDevice == nullptr || size == 0 || stride == 0)
	{
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = UINT64(size);
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
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	m_VBV.BufferLocation = m_pResource->GetGPUVirtualAddress();
	m_VBV.StrideInBytes = static_cast<UINT>(stride);
	m_VBV.SizeInBytes = static_cast<UINT>(size);

	return true;
}

bool Resource::InitAsIndexBuffer
(
	ID3D12Device* pDevice,
	DXGI_FORMAT format,
	size_t size
)
{
	if (pDevice == nullptr || size == 0)
	{
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = UINT64(size);
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
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	m_IBV.BufferLocation = m_pResource->GetGPUVirtualAddress();
	m_IBV.Format = format;
	m_IBV.SizeInBytes = static_cast<UINT>(size);

	return true;
}

bool Resource::InitAsStructuredBuffer
(
	ID3D12Device* pDevice,
	size_t count,
	size_t structureSize,
	D3D12_RESOURCE_FLAGS flags,
	D3D12_RESOURCE_STATES state,
	DescriptorPool* pPoolSRV,
	DescriptorPool* pPoolUAV
)
{
	size_t size = count * structureSize;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = static_cast<UINT64>(size);
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(count);
	srvDesc.Buffer.StructureByteStride = static_cast<UINT>(structureSize);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = static_cast<UINT>(count);
	uavDesc.Buffer.StructureByteStride = static_cast<UINT>(structureSize);
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	return Init(
		pDevice,
		size,
		heapProp,
		desc,
		state,
		pPoolSRV,
		srvDesc,
		pPoolUAV,
		uavDesc
	);
}

bool Resource::InitAsByteAddressBuffer
(
	ID3D12Device* pDevice,
	size_t size,
	D3D12_RESOURCE_FLAGS flags,
	D3D12_RESOURCE_STATES state,
	DescriptorPool* pPoolSRV,
	DescriptorPool* pPoolUAV
)
{
	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = static_cast<UINT64>(size);
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(size / 4);
	srvDesc.Buffer.StructureByteStride = 4;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = static_cast<UINT>(size / 4);
	uavDesc.Buffer.StructureByteStride = 4;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	return Init(
		pDevice,
		size,
		heapProp,
		desc,
		state,
		pPoolSRV,
		srvDesc,
		pPoolUAV,
		uavDesc
	);
}

void Resource::Term()
{
	m_pResource.Reset();
	m_pUploadBuffer.Reset(); // TODO:本当はコールバックがあればコピーのExecuteCommandLists後にすぐ消せるのでだが。

	if (m_pHandleSRV != nullptr && m_pPoolSRV != nullptr)
	{
		m_pPoolSRV->FreeHandle(m_pHandleSRV);
	}

	m_pHandleSRV = nullptr;
	m_pPoolSRV = nullptr;

	if (m_pHandleUAV != nullptr && m_pPoolUAV != nullptr)
	{
		m_pPoolUAV->FreeHandle(m_pHandleUAV);
	}

	m_pHandleUAV = nullptr;
	m_pPoolUAV = nullptr;
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
		D3D12_RESOURCE_STATE_COMMON,
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

D3D12_VERTEX_BUFFER_VIEW Resource::GetVBV() const
{
	return m_VBV;
}

D3D12_INDEX_BUFFER_VIEW Resource::GetIBV() const
{
	return m_IBV;
}

DescriptorHandle* Resource::GetHandleCBV() const
{
	// m_pHandleSRVをCBV用に使う。SRVとCBVを同人に使うことがないので。
	return m_pHandleSRV;
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
