#pragma once

#include <SimpleMath.h>
#include "App.h"
#include "VertexBuffer.h"
#include "ConstantBuffer.h"
#include "ColorTarget.h"
#include "DepthTarget.h"
#include "RootSignature.h"
#include "Texture.h"
#include "Camera.h"
#include "SphereMapConverter.h"
#include "IBLBaker.h"
#include "SkyBox.h"
#include "RenderModel.h"
#include "ResMesh.h"

class SampleApp : public App
{
public:
	SampleApp(uint32_t width, uint32_t height);
	virtual ~SampleApp();

private:
	static constexpr uint32_t NUM_POINT_LIGHTS = 4;
	static constexpr uint32_t NUM_SPOT_LIGHTS = 3;

	static constexpr uint32_t BLOOM_NUM_DOWN_SAMPLE = 6;

	Texture m_DummyTexture;
	ComPtr<ID3D12PipelineState> m_pSkyTransmittanceLUT_PSO;
	RootSignature m_SkyTransmittanceLUT_RootSig;
	ComPtr<ID3D12PipelineState> m_pSkyMultiScatteringLUT_PSO;
	RootSignature m_SkyMultiScatteringLUT_RootSig;
	ComPtr<ID3D12PipelineState> m_pSkyViewLUT_PSO;
	RootSignature m_SkyViewLUT_RootSig;
	ComPtr<ID3D12PipelineState> m_pSponzaDepthOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSponzaDepthMaskPSO;
	ComPtr<ID3D12PipelineState> m_pSponzaOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSponzaMaskPSO;
	RootSignature m_SponzaRootSig;
	ComPtr<ID3D12PipelineState> m_pSceneDepthOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneDepthMaskPSO;
	ComPtr<ID3D12PipelineState> m_pSceneOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneMaskPSO;
	RootSignature m_SceneRootSig;
	ComPtr<ID3D12PipelineState> m_pHCB_PSO;
	RootSignature m_HCB_RootSig;
	ComPtr<ID3D12PipelineState> m_pHZB_PSO;
	RootSignature m_HZB_RootSig;
	ComPtr<ID3D12PipelineState> m_pObjectVelocityPSO;
	RootSignature m_ObjectVelocityRootSig;
	ComPtr<ID3D12PipelineState> m_pCameraVelocityPSO;
	RootSignature m_CameraVelocityRootSig;
	ComPtr<ID3D12PipelineState> m_pSSAOSetupPSO;
	RootSignature m_SSAOSetupRootSig;
	ComPtr<ID3D12PipelineState> m_pSSAO_PSO;
	RootSignature m_SSAO_RootSig;
	ComPtr<ID3D12PipelineState> m_pSSGI_PSO;
	RootSignature m_SSGI_RootSig;
	ComPtr<ID3D12PipelineState> m_pSSGI_DenoisePSO;
	RootSignature m_SSGI_DenoiseRootSig;
	ComPtr<ID3D12PipelineState> m_pSSGI_TemporalAccumulationPSO;
	RootSignature m_SSGI_TemporalAccumulationRootSig;
	ComPtr<ID3D12PipelineState> m_pAmbientLightPSO;
	RootSignature m_AmbientLightRootSig;
	ComPtr<ID3D12PipelineState> m_pSSR_PSO;
	RootSignature m_SSR_RootSig;
	ComPtr<ID3D12PipelineState> m_pVolumetricFogScatteringPSO;
	RootSignature m_VolumetricFogScatteringRootSig;
	ComPtr<ID3D12PipelineState> m_pVolumetricFogIntegrationPSO;
	RootSignature m_VolumetricFogIntegrationRootSig;
	ComPtr<ID3D12PipelineState> m_pVolumetricFogCompositionPSO;
	RootSignature m_VolumetricFogCompositionRootSig;
	ComPtr<ID3D12PipelineState> m_pTemporalAA_PSO;
	RootSignature m_TemporalAA_RootSig;
	ComPtr<ID3D12PipelineState> m_pMotionBlurPSO;
	RootSignature m_MotionBlurRootSig;
	ComPtr<ID3D12PipelineState> m_pBloomSetupPSO;
	RootSignature m_BloomSetupRootSig;
	ComPtr<ID3D12PipelineState> m_pTonemapPSO;
	RootSignature m_TonemapRootSig;
	ComPtr<ID3D12PipelineState> m_pFXAA_PSO;
	RootSignature m_FXAA_RootSig;
	ComPtr<ID3D12PipelineState> m_pDownsamplePSO;
	RootSignature m_DownsampleRootSig;
	ComPtr<ID3D12PipelineState> m_pFilterPSO;
	RootSignature m_FilterRootSig;
	ComPtr<ID3D12PipelineState> m_pBackBufferPSO;
	RootSignature m_BackBufferRootSig;
	DepthTarget m_DirLightShadowMapTarget;
	DepthTarget m_SpotLightShadowMapTarget[NUM_SPOT_LIGHTS];
	ColorTarget m_SkyTransmittanceLUT_Target;
	ColorTarget m_SkyMultiScatteringLUT_Target;
	ColorTarget m_SkyViewLUT_Target;
	ColorTarget m_SceneColorTarget;
	ColorTarget m_SceneNormalTarget;
	ColorTarget m_SceneMetallicRoughnessTarget;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_HCB_Target;
	ColorTarget m_HZB_Target;
	ColorTarget m_ObjectVelocityTarget;
	ColorTarget m_VelocityTarget;
	ColorTarget m_SSAOSetupTarget;
	ColorTarget m_SSAO_HalfResTarget;
	ColorTarget m_SSAO_FullResTarget;
	Texture m_SSAO_RandomizationTex;
	ColorTarget m_SSGI_Target;
	ColorTarget m_SSGI_DenoiseTarget;
	ColorTarget m_SSGI_TemporalAccumulationTarget[FRAME_COUNT];
	ColorTarget m_AmbientLightTarget;
	ColorTarget m_SSR_Target;
	ColorTarget m_VolumetricFogScatteringTarget[FRAME_COUNT];
	ColorTarget m_VolumetricFogIntegrationTarget;
	ColorTarget m_VolumetricCompositionTarget;
	ColorTarget m_TemporalAA_Target[FRAME_COUNT];
	ColorTarget m_MotionBlurTarget;
	ColorTarget m_BloomSetupTarget[BLOOM_NUM_DOWN_SAMPLE];
	ColorTarget m_BloomHorizontalTarget[BLOOM_NUM_DOWN_SAMPLE];
	ColorTarget m_BloomVerticalTarget[BLOOM_NUM_DOWN_SAMPLE];
	ColorTarget m_TonemapTarget;
	ColorTarget m_FXAA_Target;
	VertexBuffer m_QuadVB;
	ConstantBuffer m_DirectionalLightCB[FRAME_COUNT];
	ConstantBuffer m_PointLightCB[NUM_POINT_LIGHTS];
	ConstantBuffer m_SpotLightCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_CameraCB[FRAME_COUNT];
	ConstantBuffer m_DirLightShadowMapTransformCB[FRAME_COUNT];
	ConstantBuffer m_SpotLightShadowMapTransformCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_SkyAtmosphereCB[FRAME_COUNT];
	ConstantBuffer m_TransformCB[FRAME_COUNT];
	ConstantBuffer m_HCB_CB;
	std::vector<ConstantBuffer*> m_pHZB_CBs;
	ConstantBuffer m_ObjectVelocityCB[FRAME_COUNT];
	ConstantBuffer m_CameraVelocityCB[FRAME_COUNT];
	ConstantBuffer m_SSAOSetupCB;
	ConstantBuffer m_SSAO_HalfResCB[FRAME_COUNT];
	ConstantBuffer m_SSAO_FullResCB[FRAME_COUNT];
	ConstantBuffer m_SSGI_CB;
	ConstantBuffer m_SSGI_DenoiseCB;
	ConstantBuffer m_SSGI_TemporalAccumulationCB;
	ConstantBuffer m_SSR_CB;
	ConstantBuffer m_VolumetricFogCB;
	ConstantBuffer m_TemporalAA_CB[FRAME_COUNT];
	ConstantBuffer m_MotionBlurCB;
	ConstantBuffer m_TonemapCB[FRAME_COUNT];
	ConstantBuffer m_FXAA_CB;
	ConstantBuffer m_DownsampleCB[BLOOM_NUM_DOWN_SAMPLE - 1];
	ConstantBuffer m_BloomHorizontalCB[BLOOM_NUM_DOWN_SAMPLE];
	ConstantBuffer m_BloomVerticalCB[BLOOM_NUM_DOWN_SAMPLE];
	ConstantBuffer m_BackBufferCB;
	ConstantBuffer m_IBL_CB;
	Texture m_SphereMap;
	SphereMapConverter m_SphereMapConverter;
	IBLBaker m_IBLBaker;
	SkyBox m_SkyBox;

	std::vector<Model*> m_pModels;
	std::vector<DescriptorHandle*> m_pHZB_ParentMipSRVs;
	float m_RotateAngle;
	int m_TonemapType;
	int m_ColorSpace;
	float m_BaseLuminance;
	float m_MaxLuminance;
	Camera m_Camera;
	int m_PrevCursorX;
	int m_PrevCursorY;
	D3D12_VIEWPORT m_DirLightShadowMapViewport;
	D3D12_RECT m_DirLightShadowMapScissor;
	D3D12_VIEWPORT m_SpotLightShadowMapViewport;
	D3D12_RECT m_SpotLightShadowMapScissor;
	uint32_t m_FrameNumber;
	uint32_t m_TemporalAASampleIndex;
	DirectX::SimpleMath::Matrix m_PrevWorldForMovable;
	DirectX::SimpleMath::Matrix m_PrevViewProjNoJitter;

	float m_directionalLightIntensity;
	float m_pointLightIntensity;
	float m_spotLightIntensity;
	bool m_enableVelocity;
	float m_SSAO_Contrast;
	float m_SSAO_Intensity;
	float m_SSGI_Intensity;
	float m_debugViewContrast;
	float m_SSR_Intensity;
	bool m_debugViewSSR;
	float m_BloomIntensity;
	float m_motionBlurScale;
	bool m_moveFlowerVase;
	float m_directionalLightVolumetricFogScatteringIntensity;
	float m_spotLightVolumetricFogScatteringIntensity;
	bool m_enableTemporalAA;
	bool m_enableFXAA;
	bool m_enableFXAA_HighQuality;
	int m_debugViewRenderTarget;

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;
	void ChangeDisplayMode(bool hdr);
	void DrawDirectionalLightShadowMap(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward);
	void DrawSpotLightShadowMap(ID3D12GraphicsCommandList* pCmdList, uint32_t spotLightIdx);
	void DrawSkyTransmittanceLUT(ID3D12GraphicsCommandList* pCmdList);
	void DrawSkyMultiScatteringLUT(ID3D12GraphicsCommandList* pCmdList);
	void DrawSkyViewLUT(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& skyViewLutReferential, const DirectX::SimpleMath::Vector3& dirLightDir);
	void DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward, const DirectX::SimpleMath::Matrix& viewProj, const DirectX::SimpleMath::Matrix& viewRotProj, const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj, const DirectX::SimpleMath::Matrix& skyViewLutReferential);
	void DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode);
	void DrawHCB(ID3D12GraphicsCommandList* pCmdList);
	void DrawHZB(ID3D12GraphicsCommandList* pCmdList);
	void DrawObjectVelocity(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& world, const DirectX::SimpleMath::Matrix& prevWorld, const DirectX::SimpleMath::Matrix& viewProjWithJitter, const DirectX::SimpleMath::Matrix& viewProjNoJitter, const DirectX::SimpleMath::Matrix& prevViewProjNoJitter);
	void DrawCameraVelocity(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProjNoJitter);
	void DrawSSAOSetup(ID3D12GraphicsCommandList* pCmdList);
	void DrawSSAO(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj);
	void DrawSSGI(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj, const DirectX::SimpleMath::Matrix& viewRotProj);
	void DrawSSGI_Denoise(ID3D12GraphicsCommandList* pCmdList);
	void DrawSSGI_TemporalAccumulation(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& prevTarget, const ColorTarget& curTarget);
	void DrawAmbientLight(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SSGI_CurTarget);
	void DrawSSR(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj, const DirectX::SimpleMath::Matrix& viewRotProj);
	void DrawVolumetricFogScattering(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewRotProjNoJitter, const DirectX::SimpleMath::Matrix& viewProjNoJitter, const DirectX::SimpleMath::Matrix& prevViewProjNoJitter, const ColorTarget& prevTarget, const ColorTarget& curTarget);
	void DrawVolumetricFogIntegration(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& curTarget);
	void DrawVolumetricFogComposition(ID3D12GraphicsCommandList* pCmdList);
	void DrawTemporalAA(ID3D12GraphicsCommandList* pCmdList, float temporalJitetrPixelsX, float temporalJitetrPixelsY, const ColorTarget& prevTarget, const ColorTarget& curTarget);
	void DrawMotionBlur(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& InputColor);
	void DrawBloomSetup(ID3D12GraphicsCommandList* pCmdList);
	void DrawTonemap(ID3D12GraphicsCommandList* pCmdList);
	void DrawFXAA(ID3D12GraphicsCommandList* pCmdList);
	void DrawDownsample(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SrcColor, const ColorTarget& DstColor, uint32_t CBIdx);
	void DrawFilter(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SrcColor, const ColorTarget& IntermediateColor, const ColorTarget& DstColor, const ColorTarget& DownerResultColor, const ConstantBuffer& HorizontalConstantBuffer, const ConstantBuffer& VerticalConstantBuffer);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
