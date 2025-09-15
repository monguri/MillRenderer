#pragma once

#include <d3d12.h>
#include <map>
#include <xstring>
#include <ResourceUploadBatch.h>
#include "Resource.h"
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

	template<typename CbType>
	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPool,
		Texture* pDummyTexture
	)
	{
		return Init(pDevice, pPool, sizeof(CbType), pDummyTexture);
	}

	template<typename CbType>
	bool UploadConstantBufferData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		const CbType& pData
	)
	{
		return m_CB.UploadBufferTypeData<CbType>(
			pDevice,
			pCmdList,
			1,
			&pData
		);
	}

	void Term();

	bool SetTexture
	(
		TEXTURE_USAGE usage,
		const std::wstring& path,
		DirectX::ResourceUploadBatch& batch
	);

	D3D12_GPU_DESCRIPTOR_HANDLE GetBufferHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(TEXTURE_USAGE usage) const;

	bool GetDoubleSided() const;
	void SetDoubleSided(bool isDoubleSided );

private:
	Texture* m_pDummyTexture = nullptr;
	Resource m_CB;
	Texture m_Textures[TEXTURE_USAGE_COUNT];
	bool m_DoubleSided;
	ID3D12Device* m_pDevice;
	class DescriptorPool* m_pPool;

	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPool,
		size_t cbSize,
		Texture* pDummyTexture
	);

	Material(const Material&) = delete;
	void operator=(const Material&) = delete;
};
