#pragma once

#include <d3d12.h>
#include <SimpleMath.h>
#include "ComPtr.h"
#include "ConstantBuffer.h"
#include "VertexBuffer.h"
#include "RootSignature.h"

class SkyBox
{
public:
	SkyBox();
	~SkyBox();

	bool InitSkyAtmosphere
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPoolRes,
		DXGI_FORMAT colorFormat,
		DXGI_FORMAT normalFormat,
		DXGI_FORMAT metallicRoughnessFormat,
		DXGI_FORMAT depthFormat,
		uint32_t skyViewLutWidth,
		uint32_t skyViewLutHeight,
		float planetBottomRadiusKm,
		float planetTopRadiusKm
	);

	bool InitEnvironmentCubeMap
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPoolRes,
		DXGI_FORMAT colorFormat,
		DXGI_FORMAT normalFormat,
		DXGI_FORMAT metallicRoughnessFormat,
		DXGI_FORMAT depthFormat
	);

	void DrawSkyAtmosphere
	(
		ID3D12GraphicsCommandList* pCmd,
		const class ColorTarget& skyViewLUT_Target,
		const class ColorTarget& skyTransmittanceLUT_Target,
		const struct DirectX::SimpleMath::Matrix& viewMatrix,
		const struct DirectX::SimpleMath::Matrix& projMatrix,
		const struct DirectX::SimpleMath::Matrix& viewRotProjMatrix,
		float boxSize,
		const struct DirectX::SimpleMath::Matrix& skyViewLutReferential,
		float planetBottomRadiusKm,
		const DirectX::SimpleMath::Vector3& atmosphereLightDirection,
		const DirectX::SimpleMath::Vector3& atmosphereLightLuminance
	);

	void DrawEnvironmentCubeMap
	(
		ID3D12GraphicsCommandList* pCmd,
		D3D12_GPU_DESCRIPTOR_HANDLE cubeMapHandle,
		const struct DirectX::SimpleMath::Matrix& viewMatrix,
		const struct DirectX::SimpleMath::Matrix& projMatrix,
		float boxSize
	);

	void Term();

private:
	class DescriptorPool* m_pPoolRes;
	RootSignature m_pRootSig;
	ComPtr<ID3D12PipelineState> m_pPSO;
	ConstantBuffer m_CB[2];
	VertexBuffer m_VB;
	int m_Index;

	bool Init(
		ID3D12Device* pDevice,
		class DescriptorPool* pPoolRes,
		DXGI_FORMAT colorFormat,
		DXGI_FORMAT normalFormat,
		DXGI_FORMAT metallicRoughnessFormat,
		DXGI_FORMAT depthFormat,
		const wchar_t* vsFileName,
		const wchar_t* psFileName
	);

	void DrawBox(ID3D12GraphicsCommandList* pCmd);
};
