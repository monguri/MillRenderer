#include "ColorTarget.h"
#include "DescriptorPool.h"

namespace
{
	DXGI_FORMAT ConvertToSRGB(DXGI_FORMAT format)
	{
		DXGI_FORMAT result = format;

		switch (format)
		{
			case DXGI_FORMAT_R8G8B8A8_UNORM:
				result = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC1_UNORM:
				result = DXGI_FORMAT_BC1_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC2_UNORM:
				result = DXGI_FORMAT_BC2_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC3_UNORM:
				result = DXGI_FORMAT_BC3_UNORM_SRGB;
				break;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
				result = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_B8G8R8X8_UNORM:
				result = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC7_UNORM:
				result = DXGI_FORMAT_BC7_UNORM_SRGB;
				break;
			default:
				break;
		}

		return result;
	}

	DXGI_FORMAT ConvertToUAVFormat(DXGI_FORMAT format)
	{
		DXGI_FORMAT result = format;

		switch (format)
		{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8A8_UNORM;

			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8X8_UNORM;

			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_R32_FLOAT:
				return DXGI_FORMAT_R32_FLOAT;
			default:
				return result;
		}
	}
}

ColorTarget::ColorTarget()
: m_pTarget(nullptr)
, m_pHandleRTV(nullptr)
, m_pPoolRTV(nullptr)
, m_pPoolUAV(nullptr)
, m_pHandleSRV(nullptr)
, m_pPoolSRV(nullptr)
{
	m_ClearColor[0] = 0.0f;
	m_ClearColor[1] = 0.0f;
	m_ClearColor[2] = 0.0f;
	m_ClearColor[3] = 1.0f;
}

ColorTarget::~ColorTarget()
{
	Term();
}

bool ColorTarget::InitRenderTarget
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolRTV,
	DescriptorPool* pPoolSRV,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	float clearColor[4],
	uint32_t mipLevels,
	LPCWSTR name
)
{
	if (pDevice == nullptr || pPoolRTV == nullptr || width == 0 || height == 0)
	{
		return false;
	}

	assert(m_pPoolRTV == nullptr);
	assert(m_pHandleRTV == nullptr);

	m_pPoolRTV = pPoolRTV;
	m_pPoolRTV->AddRef();

	m_pHandleRTV = pPoolRTV->AllocHandle();
	if (m_pHandleRTV == nullptr)
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
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = UINT64(width);
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = mipLevels;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	m_ClearColor[0] = clearColor[0];
	m_ClearColor[1] = clearColor[1];
	m_ClearColor[2] = clearColor[2];
	m_ClearColor[3] = clearColor[3];

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = format;
	clearValue.Color[0] = clearColor[0];
	clearValue.Color[1] = clearColor[1];
	clearValue.Color[2] = clearColor[2];
	clearValue.Color[3] = clearColor[3];

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(m_pTarget.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	if (name != nullptr)
	{
		hr = m_pTarget->SetName(name);
		if (FAILED(hr))
		{
			return false;
		}
	}

	m_RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	m_RTVDesc.Format = format;
	m_RTVDesc.Texture2D.MipSlice = 0;
	m_RTVDesc.Texture2D.PlaneSlice = 0;

	pDevice->CreateRenderTargetView(
		m_pTarget.Get(),
		&m_RTVDesc,
		m_pHandleRTV->HandleCPU
	);

	if (pPoolSRV != nullptr)
	{
		m_SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		m_SRVDesc.Format = format;
		m_SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_SRVDesc.Texture2D.MostDetailedMip = 0;
		m_SRVDesc.Texture2D.MipLevels = mipLevels;
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

bool ColorTarget::InitUnorderedAccessTarget
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolUAV,
	DescriptorPool* pPoolRTV,
	DescriptorPool* pPoolSRV,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	float clearColor[4],
	uint32_t mipLevels,
	uint32_t depth
)
{
	if (pDevice == nullptr || pPoolUAV == nullptr || width == 0 || height == 0)
	{
		return false;
	}

	assert(m_pPoolUAV == nullptr);
	assert(m_pHandleMipUAVs.size() == 0);

	m_pPoolUAV = pPoolUAV;
	m_pPoolUAV->AddRef();

	m_pHandleMipUAVs.reserve(mipLevels);

	for (uint32_t mip = 0; mip < mipLevels; mip++)
	{
		DescriptorHandle* pHandle = pPoolUAV->AllocHandle();
		if (pHandle == nullptr)
		{
			return false;
		}

		m_pHandleMipUAVs.push_back(pHandle);
	}

	assert(m_pPoolRTV == nullptr);
	assert(m_pHandleRTV == nullptr);

	if (pPoolRTV != nullptr)
	{
		m_pPoolRTV = pPoolRTV;
		m_pPoolRTV->AddRef();

		m_pHandleRTV = pPoolRTV->AllocHandle();
		if (m_pHandleRTV == nullptr)
		{
			return false;
		}
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
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.CreationNodeMask = 1;
	prop.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = (depth > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = UINT64(width);
	desc.Height = height;
	desc.DepthOrArraySize = depth;
	desc.MipLevels = mipLevels;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	m_ClearColor[0] = clearColor[0];
	m_ClearColor[1] = clearColor[1];
	m_ClearColor[2] = clearColor[2];
	m_ClearColor[3] = clearColor[3];

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = format;
	clearValue.Color[0] = clearColor[0];
	clearValue.Color[1] = clearColor[1];
	clearValue.Color[2] = clearColor[2];
	clearValue.Color[3] = clearColor[3];

	HRESULT hr = pDevice->CreateCommittedResource
	(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(m_pTarget.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC baseUAVDesc;
	baseUAVDesc.Format = ConvertToUAVFormat(format);
	if (depth > 1)
	{
		baseUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		baseUAVDesc.Texture3D.MipSlice = 0;
		baseUAVDesc.Texture3D.FirstWSlice = 0;
		baseUAVDesc.Texture3D.WSize = -1;
	}
	else
	{
		baseUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		baseUAVDesc.Texture2D.MipSlice = 0;
		baseUAVDesc.Texture2D.PlaneSlice = 0;
	}

	m_MipUAVDescs.reserve(mipLevels);
	for (uint32_t mip = 0; mip < mipLevels; mip++)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC mipUAVDesc = baseUAVDesc;
		if (depth > 1)
		{
			mipUAVDesc.Texture3D.MipSlice = mip;
		}
		else
		{
			mipUAVDesc.Texture2D.MipSlice = mip;
		}
		m_MipUAVDescs.emplace_back(mipUAVDesc);

		pDevice->CreateUnorderedAccessView(
			m_pTarget.Get(),
			nullptr,
			&mipUAVDesc,
			m_pHandleMipUAVs[mip]->HandleCPU
		);
	}

	if (pPoolRTV != nullptr)
	{
		m_RTVDesc.Format = format;
		if (depth > 1)
		{
			m_RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			m_RTVDesc.Texture3D.MipSlice = 0;
			m_RTVDesc.Texture3D.FirstWSlice = 0;
			m_RTVDesc.Texture3D.WSize = -1;
		}
		else
		{
			m_RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			m_RTVDesc.Texture2D.MipSlice = 0;
			m_RTVDesc.Texture2D.PlaneSlice = 0;
		}

		pDevice->CreateRenderTargetView(
			m_pTarget.Get(),
			&m_RTVDesc,
			m_pHandleRTV->HandleCPU
		);
	}

	if (pPoolSRV != nullptr)
	{
		m_SRVDesc.Format = format;
		m_SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if (depth > 1)
		{
			m_SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			m_SRVDesc.Texture3D.MostDetailedMip = 0;
			m_SRVDesc.Texture3D.MipLevels = mipLevels;
			m_SRVDesc.Texture3D.ResourceMinLODClamp = 0;
		}
		else
		{
			m_SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			m_SRVDesc.Texture2D.MostDetailedMip = 0;
			m_SRVDesc.Texture2D.MipLevels = mipLevels;
			m_SRVDesc.Texture2D.PlaneSlice = 0;
			m_SRVDesc.Texture2D.ResourceMinLODClamp = 0;
		}

		pDevice->CreateShaderResourceView(
			m_pTarget.Get(),
			&m_SRVDesc,
			m_pHandleSRV->HandleCPU
		);
	}

	return true;
}

bool ColorTarget::InitFromBackBuffer
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolRTV,
	bool useSRGB,
	uint32_t index,
	IDXGISwapChain* pSwapChain
)
{
	if (pDevice == nullptr || pPoolRTV == nullptr || pSwapChain == nullptr)
	{
		return false;
	}

	assert(m_pPoolRTV == nullptr);
	assert(m_pHandleRTV == nullptr);

	m_pPoolRTV = pPoolRTV;
	m_pPoolRTV->AddRef();

	m_pHandleRTV = pPoolRTV->AllocHandle();
	if (m_pHandleRTV == nullptr)
	{
		return false;
	}

	HRESULT hr = pSwapChain->GetBuffer(index, IID_PPV_ARGS(m_pTarget.GetAddressOf()));
	if (FAILED(hr))
	{
		return false;
	}

	DXGI_SWAP_CHAIN_DESC desc;
	pSwapChain->GetDesc(&desc);

	DXGI_FORMAT format = desc.BufferDesc.Format;
	if (useSRGB)
	{
		format = ConvertToSRGB(format);
	}

	m_RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	m_RTVDesc.Format = format;
	m_RTVDesc.Texture2D.MipSlice = 0;
	m_RTVDesc.Texture2D.PlaneSlice = 0;

	pDevice->CreateRenderTargetView(
		m_pTarget.Get(),
		&m_RTVDesc,
		m_pHandleRTV->HandleCPU
	);

	return true;
}

void ColorTarget::Term()
{
	m_pTarget.Reset();

	if (m_pHandleRTV != nullptr && m_pPoolRTV != nullptr)
	{
		m_pPoolRTV->FreeHandle(m_pHandleRTV);
		m_pHandleRTV = nullptr;
	}

	if (m_pPoolRTV != nullptr)
	{
		m_pPoolRTV->Release();
		m_pPoolRTV = nullptr;
	}

	for (DescriptorHandle* pHandleUAV : m_pHandleMipUAVs)
	{
		if (pHandleUAV != nullptr && m_pPoolUAV != nullptr)
		{
			m_pPoolUAV->FreeHandle(pHandleUAV);
		}
	}

	m_pHandleMipUAVs.clear();

	if (m_pPoolUAV != nullptr)
	{
		m_pPoolUAV->Release();
		m_pPoolUAV = nullptr;
	}

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
}

DescriptorHandle* ColorTarget::GetHandleRTV() const
{
	return m_pHandleRTV;
}

const std::vector<DescriptorHandle*>& ColorTarget::GetHandleUAVs() const
{
	return m_pHandleMipUAVs;
}

DescriptorHandle* ColorTarget::GetHandleSRV() const
{
	return m_pHandleSRV;
}

ID3D12Resource* ColorTarget::GetResource() const
{
	return m_pTarget.Get();
}

D3D12_RESOURCE_DESC ColorTarget::GetDesc() const
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

D3D12_RENDER_TARGET_VIEW_DESC ColorTarget::GetRTVDesc() const
{
	return m_RTVDesc;
}

const std::vector<D3D12_UNORDERED_ACCESS_VIEW_DESC>& ColorTarget::GetUAVDescs() const
{
	return m_MipUAVDescs;
}

D3D12_SHADER_RESOURCE_VIEW_DESC ColorTarget::GetSRVDesc() const
{
	return m_SRVDesc;
}

void ColorTarget::ClearView(ID3D12GraphicsCommandList* pCmdList)
{
	if (m_pHandleRTV != nullptr)
	{
		pCmdList->ClearRenderTargetView(m_pHandleRTV->HandleCPU, m_ClearColor, 0, nullptr);
	}
}
