#include "DepthTarget.h"
#include "DescriptorPool.h"

DepthTarget::DepthTarget()
: m_pTarget(nullptr)
, m_pHandleDSV(nullptr)
, m_pPoolDSV(nullptr)
{
}

DepthTarget::~DepthTarget()
{
	Term();
}

bool DepthTarget::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolDSV,
	DescriptorPool* pPoolSRV,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	float clearDepth,
	uint8_t clearStencil
)
{
	if (pDevice == nullptr || pPoolDSV == nullptr || width == 0 || height == 0)
	{
		return false;
	}

	assert(m_pPoolDSV == nullptr);
	assert(m_pHandleDSV == nullptr);

	m_pPoolDSV = pPoolDSV;
	m_pPoolDSV->AddRef();

	m_pHandleDSV = pPoolDSV->AllocHandle();
	if (m_pHandleDSV == nullptr)
	{
		return false;
	}

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

	D3D12_HEAP_PROPERTIES prop = {};
	prop.Type = D3D12_HEAP_TYPE_DEFAULT;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CreationNodeMask = 0;
	prop.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = UINT64(width);
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	m_ClearDepth = clearDepth;
	m_ClearStencil = clearStencil;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = format;
	clearValue.DepthStencil.Depth = clearDepth;
	clearValue.DepthStencil.Stencil = clearStencil;

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(m_pTarget.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	m_DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_DSVDesc.Format = format;
	m_DSVDesc.Texture2D.MipSlice = 0;
	m_DSVDesc.Flags = D3D12_DSV_FLAG_NONE;

	pDevice->CreateDepthStencilView
	(
		m_pTarget.Get(),
		&m_DSVDesc,
		m_pHandleDSV->HandleCPU
	);

	if (pPoolSRV != nullptr)
	{
		m_SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		m_SRVDesc.Format = format;
		m_SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_SRVDesc.Texture2D.MostDetailedMip = 0;
		m_SRVDesc.Texture2D.MipLevels = 1;
		m_SRVDesc.Texture2D.PlaneSlice = 0;
		m_SRVDesc.Texture2D.ResourceMinLODClamp = 0;

		pDevice->CreateShaderResourceView(
			m_pTarget.Get(),
			&m_SRVDesc,
			m_pHandleSRV->HandleCPU
		);
	}

	return true;
}

void DepthTarget::Term()
{
	m_pTarget.Reset();

	if (m_pHandleDSV != nullptr && m_pPoolDSV != nullptr)
	{
		m_pPoolDSV->FreeHandle(m_pHandleDSV);
		m_pHandleDSV = nullptr;
	}

	if (m_pPoolDSV != nullptr)
	{
		m_pPoolDSV->Release();
		m_pPoolDSV = nullptr;
	}
}

DescriptorHandle* DepthTarget::GetHandleDSV() const
{
	return m_pHandleDSV;
}

ID3D12Resource* DepthTarget::GetResource() const
{
	return m_pTarget.Get();
}

D3D12_RESOURCE_DESC DepthTarget::GetDesc() const
{
	if (m_pTarget == nullptr)
	{
		return D3D12_RESOURCE_DESC();
	}
	else
	{
		return m_pTarget->GetDesc();
	}
}

D3D12_DEPTH_STENCIL_VIEW_DESC DepthTarget::GetDSVDesc() const
{
	return m_DSVDesc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC DepthTarget::GetSRVDesc() const
{
	return m_SRVDesc;
}

void DepthTarget::ClearView(ID3D12GraphicsCommandList* pCmdList)
{
	pCmdList->ClearDepthStencilView(m_pHandleDSV->HandleCPU, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, m_ClearDepth, m_ClearStencil, 0, nullptr);
}
