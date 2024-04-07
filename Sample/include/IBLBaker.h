#pragma once
#include <d3d12.h>
#include <stdint.h>
#include "ComPtr.h"
#include "ConstantBuffer.h"
#include "VertexBuffer.h"

class DescriptorPool;
class DescriptorHandle;

class IBLBaker
{
public:
	static const int DFGTextureSize = 1024;
	static const int LDTextureSize = 256;
	static const int MipCount = 8;

	IBLBaker();
	~IBLBaker();

	bool Init
	(
		ID3D12Device* pDevice,
		DescriptorPool* pPoolRes,
		DescriptorPool* pPoolRTV
	);

	void Term();

	void IntegrateDFG(ID3D12GraphicsCommandList* pCmdList);
	void IntegrateLD(ID3D12GraphicsCommandList* pCmdList, uint32_t mapSize, uint32_t mipCount, D3D12_GPU_DESCRIPTOR_HANDLE handleCubeMap);

	D3D12_CPU_DESCRIPTOR_HANDLE GetHandleCPU_DFG() const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU_DFG() const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU_DiffuseLD() const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU_SpecularLD() const;

private:
	ConstantBuffer m_BakeCB[MipCount * 6];
	VertexBuffer m_QuadVB;
	ComPtr<ID3D12Resource> m_TexDFG;
	ComPtr<ID3D12Resource> m_TexDiffuseLD;
	ComPtr<ID3D12Resource> m_TexSpecularLD;
	DescriptorPool* m_pPoolRTV;
	DescriptorPool* m_pPoolRes;
	DescriptorHandle* m_pHandleRTV_DFG;
	DescriptorHandle* m_pHandleRTV_DiffuseLD[6];
	DescriptorHandle* m_pHandleRTV_SpecularLD[MipCount * 6];
	DescriptorHandle* m_pHandleSRV_DFG;
	DescriptorHandle* m_pHandleSRV_DiffuseLD;
	DescriptorHandle* m_pHandleSRV_SpecularLD;

	ComPtr<ID3D12PipelineState> m_pDFG_PSO;
	ComPtr<ID3D12PipelineState> m_pDiffuseLD_PSO;
	ComPtr<ID3D12PipelineState> m_pSpecularLD_PSO;
	ComPtr<ID3D12RootSignature> m_pDFG_RootSig;
	ComPtr<ID3D12RootSignature> m_pLD_RootSig;

	void IntegrateDiffuseLD(ID3D12GraphicsCommandList* pCmdList, D3D12_GPU_DESCRIPTOR_HANDLE handleCubeMap);
	void IntegrateSpecularLD(ID3D12GraphicsCommandList* pCmdList, D3D12_GPU_DESCRIPTOR_HANDLE handleCubeMap);
};
