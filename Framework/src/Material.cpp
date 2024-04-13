#include "Material.h"
#include "FileUtil.h"
#include "Logger.h"
#include "DescriptorPool.h"

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
	Texture* pDummyTexture
)
{
	assert(pDummyTexture != nullptr);
	m_pDummyTexture = pDummyTexture;

	if (pDevice == nullptr)
	{
		return false;
	}

	assert(bufferSize > 0);

	m_pDevice = pDevice;
	m_pDevice->AddRef();

	m_pPool = pPool;
	m_pPool->AddRef();

	if (!m_ConstantBuffer.Init(pDevice, pPool, bufferSize))
	{
		ELOG("Error : m_ConstantBuffer::Init() Failed.");
		m_ConstantBuffer.Term();
		return false;
	}

	return true;
}

void Material::Term()
{
	m_pDummyTexture = nullptr;

	m_ConstantBuffer.Term();

	for (uint32_t i = 0; i < TEXTURE_USAGE_COUNT; ++i)
	{
		m_Textures[i].Term();
	}

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
	TEXTURE_USAGE usage,
	const std::wstring& path,
	DirectX::ResourceUploadBatch& batch
)
{
	std::wstring findPath;
	if (!SearchFilePathW(path.c_str(), findPath))
	{
		// GetTextureHandle()ではダミーテクスチャのD3D12_GPU_DESCRIPTOR_HANDLEを返す
		return true;
	}

	if (PathIsDirectoryW(findPath.c_str()) != FALSE)
	{
		// GetTextureHandle()ではダミーテクスチャのD3D12_GPU_DESCRIPTOR_HANDLEを返す
		return true;
	}

	bool isSRGB = (usage == TEXTURE_USAGE_DIFFUSE) || (usage == TEXTURE_USAGE_BASE_COLOR) || (usage == TEXTURE_USAGE_SPECULAR || usage == TEXTURE_USAGE_EMISSIVE);
	if (!m_Textures[usage].Init(m_pDevice, m_pPool, findPath.c_str(), isSRGB, batch))
	{
		ELOG("Error : Texture::Init() Failed.");
		return false;
	}

	return true;
}

void Material::SetDoubleSided(bool isDoubleSided)
{
	m_DoubleSided = isDoubleSided;
}

void* Material::GetBufferPtr() const
{
	return m_ConstantBuffer.GetPtr();
}

D3D12_GPU_VIRTUAL_ADDRESS Material::GetBufferAddress() const
{
	return m_ConstantBuffer.GetAddress();
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetBufferHandle() const
{
	return m_ConstantBuffer.GetHandleGPU();
}

D3D12_GPU_DESCRIPTOR_HANDLE Material::GetTextureHandle(TEXTURE_USAGE usage) const
{
	if (m_Textures[usage].GetHandleGPU().ptr == 0)
	{
		// テクスチャが初期化されてなければダミーを用いる
		return m_pDummyTexture->GetHandleGPU();
	}
	else
	{
		return m_Textures[usage].GetHandleGPU();
	}
}

bool Material::GetDoubleSided() const
{
	return m_DoubleSided;
}
