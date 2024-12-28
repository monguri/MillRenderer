#pragma once

#include <d3d12.h>
#include <SimpleMath.h>
#include "ComPtr.h"
#include "ConstantBuffer.h"
#include "VertexBuffer.h"

class SkyBox
{
public:
	SkyBox();
	~SkyBox();

	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPoolRes,
		DXGI_FORMAT colorFormat,
		DXGI_FORMAT depthFormat
	);

	void Term();

	void DrawCubeMap
	(
		ID3D12GraphicsCommandList* pCmd,
		D3D12_GPU_DESCRIPTOR_HANDLE handleCubeMap,
		const struct DirectX::SimpleMath::Matrix& viewMatrix,
		const struct DirectX::SimpleMath::Matrix& projMatrix,
		float boxSize
	);

private:
	class DescriptorPool* m_pPoolRes;
	ComPtr<ID3D12RootSignature> m_pRootSig;
	ComPtr<ID3D12PipelineState> m_pPSO;
	ConstantBuffer m_CB[2];
	VertexBuffer m_VB;
	int m_Index;
};
