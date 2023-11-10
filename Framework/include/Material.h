#pragma once

#include <d3d12.h>
#include <map>
#include <xstring>
#include <ResourceUploadBatch.h>

class Material
{
public:
	enum TEXTURE_USAGE
	{
		TEXTURE_USAGE_DIFFUSE = 0,
		TEXTURE_USAGE_SPECULAR,
		TEXTURE_USAGE_SHININESS,
		TEXTURE_USAGE_NORMAL,

		TEXTURE_USAGE_BASE_COLOR,
		TEXTURE_USAGE_METALLIC,
		TEXTURE_USAGE_ROUGHNESS,
		TEXTURE_USAGE_METALLIC_ROUGHNESS,

		TEXTURE_USAGE_COUNT
	};

	Material();
	~Material();

	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPool,
		size_t bufferSize,
		size_t count
	);

	void Term();

	bool SetTexture
	(
		size_t index,
		TEXTURE_USAGE usage,
		const std::wstring& path,
		DirectX::ResourceUploadBatch& batch
	);

	void SetDoubleSided(size_t index, bool isDoubleSided );

	void* GetBufferPtr(size_t index) const;

	template<typename T>
	T* GetBufferPtr(size_t index) const
	{
		return reinterpret_cast<T*>(GetBufferPtr(index));
	}

	D3D12_GPU_VIRTUAL_ADDRESS GetBufferAddress(size_t index) const;
	
	D3D12_GPU_DESCRIPTOR_HANDLE GetBufferHandle(size_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(size_t index, TEXTURE_USAGE usage) const;

	bool GetDoubleSided(size_t index) const;

	size_t GetCount() const;

private:
	struct Subset
	{
		class ConstantBuffer* pConstantBuffer;
		D3D12_GPU_DESCRIPTOR_HANDLE TextureHandle[TEXTURE_USAGE_COUNT];
	};

	std::map<std::wstring, class Texture*> m_pTexture;
	std::vector<Subset> m_Subset;
	std::vector<bool> m_DoubleSided;
	ID3D12Device* m_pDevice;
	class DescriptorPool* m_pPool;

	Material(const Material&) = delete;
	void operator=(const Material&) = delete;
};
