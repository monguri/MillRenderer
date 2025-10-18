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
	size_t cbSize,
	Texture* pDummyTexture
)
{
	assert(pDummyTexture != nullptr);
	m_pDummyTexture = pDummyTexture;

	if (pDevice == nullptr)
	{
		return false;
	}

	assert(cbSize > 0);

	m_pDevice = pDevice;
	m_pDevice->AddRef();

	m_pPool = pPool;
	m_pPool->AddRef();

	// 更新を頻繁に行わない想定なのでDEFAULTヒープに置く
	m_CB.InitAsConstantBuffer(
		pDevice,
		cbSize,
		D3D12_HEAP_TYPE_DEFAULT,
		pPool
	);

	return true;
}

void Material::Term()
{
	m_pDummyTexture = nullptr;

	m_CB.Term();

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

const DescriptorHandle& Material::GetCBHandle() const
{
	return *m_CB.GetHandleCBV();
}

const DescriptorHandle& Material::GetTextureSrvHandle(TEXTURE_USAGE usage) const
{
	if (m_Textures[usage].GetHandleSRVPtr() == nullptr)
	{
		// テクスチャが初期化されてなければダミーを用いる
		return *m_pDummyTexture->GetHandleSRVPtr();
	}
	else
	{
		return *m_Textures[usage].GetHandleSRVPtr();
	}
}

bool Material::GetDoubleSided() const
{
	return m_DoubleSided;
}

void Material::SetDoubleSided(bool isDoubleSided)
{
	m_DoubleSided = isDoubleSided;
} 
