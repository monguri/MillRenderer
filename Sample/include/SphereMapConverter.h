#pragma once

#include <d3d12.h>
#include <stdint.h>
#include <vector>
#include "ComPtr.h"
#include "ConstantBuffer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"

class DescriptorPool;
class DescriptorHandle;

class SphereMapConverter
{
public:
	SphereMapConverter();
	~SphereMapConverter();

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolRTV,
		DescriptorPool* pPoolRes,
		const D3D12_RESOURCE_DESC& sphereMapDesc,
		int mapSize = -1
	);

	void Term();

	void DrawToCube(ID3D12GraphicsCommandList* pCmdList, D3D12_GPU_DESCRIPTOR_HANDLE sphereMapHandle);

	D3D12_RESOURCE_DESC GetCubeMapDesc() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU() const;

private:
	DescriptorPool* m_pPoolRTV;
	DescriptorPool* m_pPoolRes;
	ComPtr<ID3D12RootSignature> m_pRootSig;
	ComPtr<ID3D12PipelineState> m_pPSO;
	uint32_t m_MipCount;
	ComPtr<ID3D12Resource> m_pCubeTex;
	DescriptorHandle* m_pCubeSRV;
	std::vector<DescriptorHandle*> m_pCubeRTV;
	ConstantBuffer m_TransformCB[6];
	VertexBuffer m_VB;
	IndexBuffer m_IB;

	void DrawSphere(ID3D12GraphicsCommandList* pCmdList);
};
