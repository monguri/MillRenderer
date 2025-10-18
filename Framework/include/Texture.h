#pragma once

#include <d3d12.h>
#include <ResourceUploadBatch.h>
#include "ComPtr.h"

class DescriptorPool;
class DescriptorHandle;

class Texture
{
public:
	Texture();
	~Texture();

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPool,
		const wchar_t* filename,
		bool isSRGB,
		DirectX::ResourceUploadBatch& batch
	);

	bool InitFromData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPool,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		size_t pixelSize,
		const void* pInitData
	);

	template<typename T>
	bool InitFromData
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		DescriptorPool* pPool,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		const T* pInitData
	)
	{
		return InitFromData(
			pDevice,
			pCmdList,
			pPool,
			width,
			height,
			format,
			sizeof(T),
			pInitData
		);
	}

	void Term();

	const DescriptorHandle* GetHandleSRVPtr() const;

	ID3D12Resource* GetResource() const;
	D3D12_RESOURCE_DESC GetDesc() const;

private:
	ComPtr<ID3D12Resource> m_pTex;
	ComPtr<ID3D12Resource> m_pUploadBuffer;
	DescriptorHandle* m_pHandle;
	DescriptorPool* m_pPool;

	D3D12_SHADER_RESOURCE_VIEW_DESC GetViewDesc(bool isCube) const;

	Texture(const Texture&) = delete;
	void operator=(const Texture&) = delete;
};
