#include "Material.h"
#include "FileUtil.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include "Texture.h"
#include "ConstantBuffer.h"

namespace
{
	constexpr const wchar_t* DummyTag = L"";
}

Material::Material()
: m_pDevice(nullptr)
, m_pPool(nullptr)
{
}

Material::~Material()
{
	Term();
}

bool Material::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPool,
	size_t bufferSize,
	size_t count
)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	m_pDevice = pDevice;
	m_pDevice->AddRef();

	m_pPool = pPool;
	m_pPool->AddRef();

	m_Subset.resize(count);
	m_DoubleSided.resize(count);

	// ダミーテクスチャ生成
	{
		Texture* pTexture = new (std::nothrow) Texture();
		if (pTexture == nullptr)
		{
			return false;
		}

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = 1;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		if (!pTexture->Init(pDevice, pPool, &desc, false, false))
		{
			ELOG("Error : Texture::Init() Failed.");
			pTexture->Term();
			delete pTexture;
			return false;
		}

		m_pTexture[DummyTag] = pTexture;
	}

	size_t size = bufferSize * count;
	if (size > 0)
	{
		for (size_t i = 0; i < m_Subset.size(); ++i)
		{
			ConstantBuffer* pBuffer = new (std::nothrow) ConstantBuffer();
			if (pBuffer == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!pBuffer->Init(pDevice, pPool, size))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				pBuffer->Term();
				delete pBuffer;
				return false;
			}

			m_Subset[i].pConstantBuffer = pBuffer;
			for (uint32_t j = 0; j < TEXTURE_USAGE_COUNT; ++j)
			{
				m_Subset[i].TextureHandle[j].ptr = 0;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < m_Subset.size(); ++i)
		{
			m_Subset[i].pConstantBuffer = nullptr;

			for (uint32_t j = 0; j < TEXTURE_USAGE_COUNT; ++j)
			{
				m_Subset[i].TextureHandle[j].ptr = 0;
			}
		}
	}

	return true;
}

void Material::Term()
{
	for (std::pair<std::wstring, Texture*>&& itr : m_pTexture)
	{
		itr.second->Term();
		delete itr.second;
		itr.second = nullptr;
	}

	for (size_t i = 0; i < m_Subset.size(); ++i)
	{
		if (m_Subset[i].pConstantBuffer != nullptr)
		{
			m_Subset[i].pConstantBuffer->Term();
			delete m_Subset[i].pConstantBuffer;
			m_Subset[i].pConstantBuffer = nullptr;
		}

		for (uint32_t j = 0; j < TEXTURE_USAGE_COUNT; ++j)
		{
			m_Subset[i].TextureHandle[j].ptr = 0;
		}
	}

	m_pTexture.clear();
	m_Subset.clear();
	m_DoubleSided.clear();

	if (m_pDevice != nullptr)
	{
		m_pDevice->Release();
		m_pDevice = nullptr;
	}

	if (m_pPool != nullptr)
	{
		m_pPool->Release();
		m_pPool = nullptr;
	}
}

bool Material::SetTexture
(
	size_t index,
	TEXTURE_USAGE usage,
	const std::wstring& path,
	DirectX::ResourceUploadBatch& batch
)
{
	if (index >= GetCount())
	{
		return false;
	}

	if (m_pTexture.find(path) != m_pTexture.end())
	{
		m_Subset[index].TextureHandle[usage] = m_pTexture[path]->GetHandleGPU();
		return true;
	}

	std::wstring findPath;
	if (!SearchFilePathW(path.c_str(), findPath))
	{
		m_Subset[index].TextureHandle[usage] = m_pTexture[DummyTag]->GetHandleGPU();
		return true;
	}

	if (PathIsDirectoryW(findPath.c_str()) != FALSE)
	{
		m_Subset[index].TextureHandle[usage] = m_pTexture[DummyTag]->GetHandleGPU();
		return true;
	}

	Texture* pTexture = new (std::nothrow) Texture();
	if (pTexture == nullptr)
	{
		ELOG("Error : Out of memory.");
		return false;
	}

	bool isSRGB = (usage == TEXTURE_USAGE_DIFFUSE) || (usage == TEXTURE_USAGE_BASE_COLOR) || (usage == TEXTURE_USAGE_SPECULAR);
	if (!pTexture->Init(m_pDevice, m_pPool, findPath.c_str(), isSRGB, batch))
	{
		ELOG("Error : Texture::Init() Failed.");
		pTexture->Term();
		delete pTexture;
		return false;
	}

	m_pTexture[path] = pTexture;
	m_Subset[index].TextureHandle[usage] = pTexture->GetHandleGPU();

	return true;
}

void Material::SetDoubleSided(size_t index, bool isDoubleSided)
{
	m_DoubleSided[index] = isDoubleSided;
}

void* Material::GetBufferPtr(size_t index) const
{
	if (index >= GetCount())
	{
		return nullptr;
	}

	return m_Subset[index].pConstantBuffer->GetPtr();
}

D3D12_GPU_VIRTUAL_ADDRESS Material::GetBufferAddress(size_t index) const
{
	if (index >= GetCount())
	{
		return D3D12_GPU_VIRTUAL_ADDRESS();
	}

	return m_Subset[index].pConstantBuffer->GetAddress();
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetBufferHandle(size_t index) const
{
	return m_Subset[index].pConstantBuffer->GetHandleGPU();
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetTextureHandle(size_t index, TEXTURE_USAGE usage) const
{
	if (index >= GetCount())
	{
		return D3D12_GPU_DESCRIPTOR_HANDLE();
	}

	return m_Subset[index].TextureHandle[usage];
}

bool Material::GetDoubleSided(size_t index) const
{
	return m_DoubleSided[index];
}

size_t Material::GetCount() const
{
	return m_Subset.size();
}
