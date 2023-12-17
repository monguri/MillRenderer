#include "ColorTarget.h"
#include "DescriptorPool.h"
#include <DirectXHelpers.h>

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
}

ColorTarget::ColorTarget()
: m_pTarget(nullptr)
, m_pHandleRTV(nullptr)
, m_pPoolRTV(nullptr)
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
	float clearColor[4]
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
	desc.MipLevels = 1;
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

bool ColorTarget::InitFromData
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	DescriptorPool* pPoolSRV,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	size_t pixelSize,
	const void* pInitData
)
{
	if (pDevice == nullptr || pPoolSRV == nullptr || width == 0 || height == 0 || pInitData == nullptr)
	{
		return false;
	}

	assert(m_pPoolSRV == nullptr);
	assert(m_pHandleSRV == nullptr);

	m_pPoolSRV = pPoolSRV;
	m_pPoolSRV->AddRef();

	m_pHandleSRV = pPoolSRV->AllocHandle();
	if (m_pHandleSRV == nullptr)
	{
		return false;
	}

	// Upload用バッファ作成
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	{
		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // Upload用のものはD3D12_RESOURCE_DIMENSION_TEXTURE2DでなくBufferで作らねばならない
		desc.Alignment = 0;
		desc.Width = pixelSize * width * height;
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

		memcpy(ptr, pInitData, pixelSize * width * height);

		m_pUploadBuffer->Unmap(0, nullptr);
	}

	// テクスチャ作成とコピー
	{
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
		desc.MipLevels = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		HRESULT hr = pDevice->CreateCommittedResource
		(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COPY_DEST, // コピー用の状態にしておく
			nullptr,
			IID_PPV_ARGS(m_pTarget.GetAddressOf())
		);
		if (FAILED(hr))
		{
			return false;
		}

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

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		pDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);

		D3D12_TEXTURE_COPY_LOCATION src;
		src.pResource = m_pUploadBuffer.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		D3D12_TEXTURE_COPY_LOCATION dst;
		dst.pResource = m_pTarget.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		pCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		DirectX::TransitionResource(pCmdList, GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	m_pUploadBuffer.Reset();

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

D3D12_SHADER_RESOURCE_VIEW_DESC ColorTarget::GetSRVDesc() const
{
	return m_SRVDesc;
}

void ColorTarget::ClearView(ID3D12GraphicsCommandList* pCmdList)
{
	pCmdList->ClearRenderTargetView(m_pHandleRTV->HandleCPU, m_ClearColor, 0, nullptr);
}
