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

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPool,
		const D3D12_RESOURCE_DESC* pDesc,
		bool isSRGB,
		bool isCube
	);

	void Term();

	D3D12_CPU_DESCRIPTOR_HANDLE GetHandleCPU() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU() const;

	ID3D12Resource* GetResource() const;

private:
	ComPtr<ID3D12Resource> m_pTex;
	DescriptorHandle* m_pHandle;
	DescriptorPool* m_pPool;

	D3D12_SHADER_RESOURCE_VIEW_DESC GetViewDesc(bool isCube) const;

	Texture(const Texture&) = delete;
	void operator=(const Texture&) = delete;
};
