#include "Texture.h"
#include "DescriptorPool.h"
#include "Logger.h"
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
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

Texture::Texture()
: m_pTex(nullptr)
, m_pHandle(nullptr)
, m_pPool(nullptr)
{
}

Texture::~Texture()
{
	Term();
}

bool Texture::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPool,
	const wchar_t* filename,
	bool isSRGB,
	DirectX::ResourceUploadBatch& batch
)
{
	if (pDevice == nullptr || pPool == nullptr || filename == nullptr)
	{
		ELOG("Eror : Invalid Argument.");
		return false;
	}

	assert(m_pPool == nullptr);
	assert(m_pHandle == nullptr);

	m_pPool = pPool;
	m_pPool->AddRef();

	m_pHandle = pPool->AllocHandle();
	if (m_pHandle == nullptr)
	{
		return false;
	}

	bool isCube = false;
	DirectX::DDS_LOADER_FLAGS flag = DirectX::DDS_LOADER_MIP_AUTOGEN;
	if (isSRGB)
	{
		flag |= DirectX::DDS_LOADER_FORCE_SRGB;
	}

	HRESULT hr = DirectX::CreateDDSTextureFromFileEx
	(
		pDevice,
		batch,
		filename,
		0,
		D3D12_RESOURCE_FLAG_NONE,
		flag,
		m_pTex.GetAddressOf(),
		nullptr,
		&isCube
	);
	if (FAILED(hr))
	{
		DirectX::WIC_LOADER_FLAGS flag = DirectX::WIC_LOADER_MIP_AUTOGEN;
		if (isSRGB)
		{
			flag |= DirectX::WIC_LOADER_FORCE_SRGB;
		}

		hr = DirectX::CreateWICTextureFromFileEx
		(
			pDevice,
			batch,
			filename,
			0,
			D3D12_RESOURCE_FLAG_NONE,
			flag,
			m_pTex.GetAddressOf()
		);
		if (FAILED(hr))
		{
			ELOG("Error : CreateDDSTextureFromFileEx() and CreateWICTextureFromFileEx() Failed. filename = %ls, retcode = 0x%x", filename, hr);
			return false;
		}
	}

	const D3D12_SHADER_RESOURCE_VIEW_DESC& viewDesc = GetViewDesc(isCube);
	pDevice->CreateShaderResourceView(m_pTex.Get(), &viewDesc, m_pHandle->HandleCPU);

	return true;
}

bool Texture::InitFromData
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	DescriptorPool* pPool,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	size_t pixelSize,
	const void* pInitData
)
{
	if (pDevice == nullptr || pPool == nullptr || width == 0 || height == 0 || pInitData == nullptr)
	{
		return false;
	}

	assert(m_pPool == nullptr);
	assert(m_pHandle == nullptr);

	m_pPool = pPool;
	m_pPool->AddRef();

	m_pHandle = pPool->AllocHandle();
	if (m_pHandle == nullptr)
	{
		return false;
	}

	// テクスチャ作成
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	UINT64 texBytes;
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
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		HRESULT hr = pDevice->CreateCommittedResource
		(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(m_pTex.GetAddressOf())
		);
		if (FAILED(hr))
		{
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Format = format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0;

		pDevice->CreateShaderResourceView(
			m_pTex.Get(),
			&srvDesc,
			m_pHandle->HandleCPU
		);

		pDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &texBytes);
	}

	// Upload用バッファ作成
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
		desc.Width = texBytes;
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

		// 一要素ずつ書き込んでいくのはUEのFSystemTextures::InitializeFeatureLevelDependentTextures()を参考にしている
		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < height; x++)
			{
				uint8_t* src = (uint8_t*)pInitData + (x + y * height) * pixelSize;
				uint8_t* dest = reinterpret_cast<uint8_t*>(ptr) + x * pixelSize + y * footprint.Footprint.RowPitch;
				memcpy(dest, src, pixelSize);
			}
		}

		m_pUploadBuffer->Unmap(0, nullptr);
	}

	// テクスチャ作成とコピー
	{
		DirectX::TransitionResource(pCmdList, GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

		D3D12_TEXTURE_COPY_LOCATION src;
		src.pResource = m_pUploadBuffer.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		D3D12_TEXTURE_COPY_LOCATION dst;
		dst.pResource = m_pTex.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		pCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		DirectX::TransitionResource(pCmdList, GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	return true;
}

void Texture::Term()
{
	m_pTex.Reset();
	m_pUploadBuffer.Reset(); // TODO:本当はコピーのExecuteCommandLists後にすぐ消せるのだが。

	if (m_pHandle != nullptr && m_pPool != nullptr)
	{
		m_pPool->FreeHandle(m_pHandle);
		m_pHandle = nullptr;
	}

	if (m_pPool != nullptr)
	{
		m_pPool->Release();
		m_pPool = nullptr;
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetHandleCPU() const
{
	if (m_pHandle != nullptr)
	{
		return m_pHandle->HandleCPU;
	}

	return D3D12_CPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetHandleGPU() const
{
	if (m_pHandle != nullptr)
	{
		return m_pHandle->HandleGPU;
	}

	return D3D12_GPU_DESCRIPTOR_HANDLE();
}

ID3D12Resource* Texture::GetResource() const
{
	return m_pTex.Get();
}

D3D12_RESOURCE_DESC Texture::GetDesc() const
{
	if (m_pTex == nullptr)
	{
		return D3D12_RESOURCE_DESC();
	}
	else
	{
		return m_pTex->GetDesc();
	}
}

D3D12_SHADER_RESOURCE_VIEW_DESC Texture::GetViewDesc(bool isCube) const
{
	D3D12_SHADER_RESOURCE_VIEW_DESC  viewDesc = {};
	const D3D12_RESOURCE_DESC& desc = m_pTex->GetDesc();

	viewDesc.Format = desc.Format;
	viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	switch (desc.Dimension)
	{
		case D3D12_RESOURCE_DIMENSION_BUFFER:
		{
			abort();
		}
			break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
		{
			if (desc.DepthOrArraySize > 1)
			{
				viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;

				viewDesc.Texture1DArray.MostDetailedMip = 0;
				viewDesc.Texture1DArray.MipLevels = desc.MipLevels;
				viewDesc.Texture1DArray.FirstArraySlice = 0;
				viewDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
				viewDesc.Texture1DArray.ResourceMinLODClamp = 0.0f;
			}
			else
			{
				viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;

				viewDesc.Texture1D.MostDetailedMip = 0;
				viewDesc.Texture1D.MipLevels = desc.MipLevels;
				viewDesc.Texture1D.ResourceMinLODClamp = 0.0f;
			}
		}
			break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
		{
			if (isCube)
			{
				if (desc.DepthOrArraySize > 6)
				{
					viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;

					viewDesc.TextureCubeArray.MostDetailedMip = 0;
					viewDesc.TextureCubeArray.MipLevels = desc.MipLevels;
					viewDesc.TextureCubeArray.First2DArrayFace = 0;
					viewDesc.TextureCubeArray.NumCubes = desc.DepthOrArraySize / 6;
					viewDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
				}
				else
				{
					viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;

					viewDesc.TextureCube.MostDetailedMip = 0;
					viewDesc.TextureCube.MipLevels = desc.MipLevels;
					viewDesc.TextureCube.ResourceMinLODClamp = 0.0f;
				}
			}
			else
			{
				if (desc.DepthOrArraySize > 1)
				{
					viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;

					viewDesc.Texture2DArray.MostDetailedMip = 0;
					viewDesc.Texture2DArray.MipLevels = desc.MipLevels;
					viewDesc.Texture2DArray.FirstArraySlice = 0;
					viewDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
					viewDesc.Texture2DArray.PlaneSlice = 0;
					viewDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
				}
				else
				{
					viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

					viewDesc.Texture2D.MostDetailedMip = 0;
					viewDesc.Texture2D.MipLevels = desc.MipLevels;
					viewDesc.Texture2D.PlaneSlice = 0;
					viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
				}
			}
		}
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
		{
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;

			viewDesc.Texture2D.MostDetailedMip = 0;
			viewDesc.Texture2D.MipLevels = desc.MipLevels;
			viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		}
			break;
		default:
		{
			abort();
		}
			break;
	}

	return viewDesc;
}
