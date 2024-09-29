#include "StructuredBuffer.h"
#include "DescriptorPool.h"
#include <assert.h>

StructuredBuffer::StructuredBuffer()
: m_pBuffer(nullptr)
, m_pHandleSRV(nullptr)
, m_pHandleUAV(nullptr)
, m_pPoolSRV(nullptr)
, m_pPoolUAV(nullptr)
{
}

StructuredBuffer::~StructuredBuffer()
{
	Term();
}

bool StructuredBuffer::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolSRV,
	DescriptorPool* pPoolUAV,
	size_t count,
	size_t structureSize,
	bool useUAV,
	const void* pInitData
)
{
	if (pDevice == nullptr || pPoolSRV == nullptr || (useUAV && pPoolUAV == nullptr) || count == 0 || structureSize == 0)
	{
		return false;
	}

	size_t dataSize = count * structureSize;

	assert(m_pPoolSRV == nullptr);
	assert(m_pHandleSRV == nullptr);

	m_pPoolSRV = pPoolSRV;
	m_pPoolSRV->AddRef();

	m_pHandleSRV = pPoolSRV->AllocHandle();
	if (m_pHandleSRV == nullptr)
	{
		return false;
	}

	assert(m_pPoolUAV == nullptr);
	assert(m_pHandleUAV == nullptr);

	if (useUAV)
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
	prop.Type = D3D12_HEAP_TYPE_UPLOAD;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = UINT64(dataSize);
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
		IID_PPV_ARGS(m_pBuffer.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	if (pInitData != nullptr)
	{
		void* ptr = Map();
		if (ptr == nullptr)
		{
			return false;
		}

		memcpy(ptr, pInitData, dataSize);

		Unmap();
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (UINT)count;
	srvDesc.Buffer.StructureByteStride = (UINT)structureSize;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	pDevice->CreateShaderResourceView(
		m_pBuffer.Get(),
		&srvDesc,
		m_pHandleSRV->HandleCPU
	);

	if (useUAV)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = (UINT)count;
		uavDesc.Buffer.StructureByteStride = (UINT)structureSize;
		uavDesc.Buffer.CounterOffsetInBytes = 0;

		pDevice->CreateUnorderedAccessView(
			m_pBuffer.Get(),
			nullptr,
			&uavDesc,
			m_pHandleUAV->HandleCPU
		);
	}

	return true;
}

void StructuredBuffer::Term()
{
	m_pBuffer.Reset();

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
}

void* StructuredBuffer::Map() const
{
	void* ptr;
	HRESULT hr = m_pBuffer->Map(0, nullptr, &ptr);
	if (FAILED(hr))
	{
		return nullptr;
	}

	return ptr;
}

void StructuredBuffer::Unmap() const
{
	m_pBuffer->Unmap(0, nullptr);
}

DescriptorHandle* StructuredBuffer::GetHandleSRV() const
{
	return m_pHandleSRV;
}

DescriptorHandle* StructuredBuffer::GetHandleUAV() const
{
	return m_pHandleUAV;
}
