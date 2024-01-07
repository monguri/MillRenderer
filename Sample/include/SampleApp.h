#pragma once

#include <SimpleMath.h>
#include "App.h"
#include "Material.h"
#include "VertexBuffer.h"
#include "ConstantBuffer.h"
#include "ColorTarget.h"
#include "DepthTarget.h"
#include "RootSignature.h"
#include "Texture.h"
#include "Camera.h"
#include "ResMesh.h"

class SampleApp : public App
{
public:
	SampleApp(uint32_t width, uint32_t height);
	virtual ~SampleApp();

private:
	static constexpr uint32_t DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE = 2048; // TODO:ModelViewerÇéQçlÇ…ÇµÇΩ
	static constexpr uint32_t NUM_POINT_LIGHTS = 4;
	static constexpr uint32_t NUM_SPOT_LIGHTS = 3;

	ComPtr<ID3D12PipelineState> m_pSceneDepthOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneDepthMaskPSO;
	ComPtr<ID3D12PipelineState> m_pSceneOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneMaskPSO;
	RootSignature m_SceneRootSig;
	ComPtr<ID3D12PipelineState> m_pSSAO_PSO;
	RootSignature m_SSAO_RootSig;
	ComPtr<ID3D12PipelineState> m_pAmbientLightPSO;
	RootSignature m_AmbientLightRootSig;
	ComPtr<ID3D12PipelineState> m_pTemporalAA_PSO;
	RootSignature m_TemporalAA_RootSig;
	ComPtr<ID3D12PipelineState> m_pTonemapPSO;
	RootSignature m_TonemapRootSig;
	ComPtr<ID3D12PipelineState> m_pDebugRenderTargetPSO;
	RootSignature m_DebugRenderTargetRootSig;
	DepthTarget m_DirLightShadowMapTarget;
	DepthTarget m_SpotLightShadowMapTarget[NUM_SPOT_LIGHTS];
	ColorTarget m_SceneColorTarget;
	ColorTarget m_SceneNormalTarget;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_SSAO_Target;
	ColorTarget m_SSAO_RandomizationTarget;
	ColorTarget m_AmbientLightTarget;
	ColorTarget m_TemporalAA_Target[FRAME_COUNT];
	ColorTarget m_TemporalAA_HalfResTarget;
	VertexBuffer m_QuadVB;
	ConstantBuffer m_DirectionalLightCB[FRAME_COUNT];
	ConstantBuffer m_PointLightCB[NUM_POINT_LIGHTS];
	ConstantBuffer m_SpotLightCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_CameraCB[FRAME_COUNT];
	ConstantBuffer m_DirLightShadowMapTransformCB[FRAME_COUNT];
	ConstantBuffer m_SpotLightShadowMapTransformCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_TransformCB[FRAME_COUNT];
	ConstantBuffer m_MeshCB;
	ConstantBuffer m_SSAO_CB[FRAME_COUNT];
	ConstantBuffer m_TemporalAA_CB[FRAME_COUNT];
	ConstantBuffer m_TonemapCB[FRAME_COUNT];
	std::vector<class Mesh*> m_pMesh;
	Material m_Material;
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
	uint32_t m_TemporalAASampleIndex;
	DirectX::SimpleMath::Matrix m_PrevViewProjNoJitter;

	virtual bool OnInit() override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual void OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;
	void ChangeDisplayMode(bool hdr);
	void DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward, const DirectX::SimpleMath::Matrix& viewProj);
	void DrawDirectionalLightShadowMap(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward);
	void DrawSpotLightShadowMap(ID3D12GraphicsCommandList* pCmdList, uint32_t spotLightIdx);
	void DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode);
	void DrawSSAO(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& projWithJitter);
	void DrawAmbientLight(ID3D12GraphicsCommandList* pCmdList);
	void DrawTemporalAA(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProjNoJitter, uint32_t TempAA_SrcIdx, uint32_t TempAA_DstIdx);
	void DrawTonemap(ID3D12GraphicsCommandList* pCmdList, uint32_t TempAA_DstIdx);
	void DebugDrawSSAO(ID3D12GraphicsCommandList* pCmdList);
};
