#pragma once

#include <d3d12.h>
#include <map>
#include <xstring>
#include <ResourceUploadBatch.h>
#include "ConstantBuffer.h"
#include "Texture.h"

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
		TEXTURE_USAGE_EMISSIVE,
		TEXTURE_USAGE_AMBIENT_OCCLUSION,

		TEXTURE_USAGE_COUNT
	};

	Material();
	~Material();

	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPool,
		size_t bufferSize,
		Texture* pDummyTexture
	);

	void Term();

	bool SetTexture
	(
		TEXTURE_USAGE usage,
		const std::wstring& path,
		DirectX::ResourceUploadBatch& batch
	);

	void SetDoubleSided(bool isDoubleSided );

	void* GetBufferPtr() const;

	template<typename T>
	T* GetBufferPtr() const
	{
		return reinterpret_cast<T*>(GetBufferPtr());
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetBufferHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(TEXTURE_USAGE usage) const;

	bool GetDoubleSided() const;

private:
	Texture* m_pDummyTexture = nullptr;
	ConstantBuffer m_ConstantBuffer;
	Texture m_Textures[TEXTURE_USAGE_COUNT];
	bool m_DoubleSided;
	ID3D12Device* m_pDevice;
	class DescriptorPool* m_pPool;

	Material(const Material&) = delete;
	void operator=(const Material&) = delete;
};
