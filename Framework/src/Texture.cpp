#include "Texture.h"
#include "DescriptorPool.h"
#include "Logger.h"
#include <DDSTextureLoader.h>

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
		ELOG("Error : DirectX::CreateDDSTextureFromFile() Failed. filename = %ls, retcode = 0x%x", filename, hr);
		return false;
	}

	const D3D12_SHADER_RESOURCE_VIEW_DESC& viewDesc = GetViewDesc(isCube);
	pDevice->CreateShaderResourceView(m_pTex.Get(), &viewDesc, m_pHandle->HandleCPU);

	return true;
}

bool Texture::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPool,
	const D3D12_RESOURCE_DESC* pDesc,
	bool isSRGB,
	bool isCube
)
{
	if (pDevice == nullptr || pPool == nullptr || pDesc == nullptr)
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

	D3D12_RESOURCE_DESC desc = *pDesc;
	if (isSRGB)
	{
		desc.Format = ConvertToSRGB(desc.Format);
	}

	D3D12_HEAP_PROPERTIES prop = {};
	prop.Type = D3D12_HEAP_TYPE_DEFAULT;
	prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	prop.CreationNodeMask = 0;
	prop.VisibleNodeMask = 0;

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
		ELOG("Eror : ID3D12Device::CreateCommittedResource() Failed. retcode = 0x%x", hr);
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = GetViewDesc(isCube);
#if 0 // GetViewDesc()の中でm_pTexのFormatをコピーしているので不要。
	if (isSRGB)
	{
		viewDesc.Format = ConvertToSRGB(viewDesc.Format);
	}
#endif

	pDevice->CreateShaderResourceView(m_pTex.Get(), &viewDesc, m_pHandle->HandleCPU);

	return true;
}

void Texture::Term()
{
	m_pTex.Reset();

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
					if (desc.MipLevels > 1)
					{
						viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;

						viewDesc.Texture2DMSArray.FirstArraySlice = 0;
						viewDesc.Texture2DMSArray.ArraySize = desc.DepthOrArraySize;
					}
					else
					{
						viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;

						viewDesc.Texture2DArray.MostDetailedMip = 0;
						viewDesc.Texture2DArray.MipLevels = desc.MipLevels;
						viewDesc.Texture2DArray.FirstArraySlice = 0;
						viewDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
						viewDesc.Texture2DArray.PlaneSlice = 0;
						viewDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
					}
				}
				else
				{
					if (desc.MipLevels > 1)
					{
						viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
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
