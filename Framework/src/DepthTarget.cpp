#include "DepthTarget.h"
#include "DescriptorPool.h"

namespace
{
	//
	// referenced PixelBuffer::GetDSVFormat() of Model Viewer.
	//

	DXGI_FORMAT GetBaseFormat(DXGI_FORMAT defaultFormat)
	{
		switch (defaultFormat)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;

		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;

		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8X8_TYPELESS;

		// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32G8X24_TYPELESS;

		// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_TYPELESS;

		// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24G8_TYPELESS;

		// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_TYPELESS;

		default:
			return defaultFormat;
		}
	}

	DXGI_FORMAT GetUAVFormat(DXGI_FORMAT defaultFormat)
	{
		switch (defaultFormat)
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

	#ifdef _DEBUG
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_D16_UNORM:

			// Requested a UAV Format for a depth stencil Format.
			assert(false);
	#endif

		default:
			return defaultFormat;
		}
	}

	DXGI_FORMAT GetDSVFormat(DXGI_FORMAT defaultFormat)
	{
		switch (defaultFormat)
		{
		// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

		// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_D32_FLOAT;

		// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;

		// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_D16_UNORM;

		default:
			return defaultFormat;
		}
	}

	DXGI_FORMAT GetDepthFormat(DXGI_FORMAT defaultFormat)
	{
		switch (defaultFormat)
		{
		// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

		// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

		// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;

		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}
}

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
	uint8_t clearStencil,
	LPCWSTR name
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
	desc.Format = GetBaseFormat(format);
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	m_ClearDepth = clearDepth;
	m_ClearStencil = clearStencil;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = GetDSVFormat(format);
	clearValue.DepthStencil.Depth = clearDepth;
	clearValue.DepthStencil.Stencil = clearStencil;

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

	m_DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_DSVDesc.Format = GetDSVFormat(format);
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
		m_SRVDesc.Format = GetDepthFormat(format);
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

DescriptorHandle* DepthTarget::GetHandleSRV() const
{
	return m_pHandleSRV;
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
