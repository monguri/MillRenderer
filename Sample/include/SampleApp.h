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
	static constexpr float CAMERA_FOV_Y_DEGREE = 37.5f;
	static constexpr float CAMERA_NEAR = 0.1f;
	static constexpr float CAMERA_FAR = 100.0f;
	static constexpr uint32_t DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE = 2048; // TODO:ModelViewerを参考にした
	static constexpr uint32_t SPOT_LIGHT_SHADOW_MAP_SIZE = 512; // TODO:ModelViewerを参考にした
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
	ComPtr<ID3D12PipelineState> m_pTonemapPSO;
	RootSignature m_TonemapRootSig;
	DepthTarget m_DirLightShadowMapTarget;
	DepthTarget m_SpotLightShadowMapTarget[NUM_SPOT_LIGHTS];
	ColorTarget m_SceneColorTarget;
	ColorTarget m_SceneNormalTarget;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_SSAO_Target;
	ColorTarget m_SSAO_RandomizationTarget;
	ColorTarget m_AmbientLightTarget;
	VertexBuffer m_QuadVB;
	ConstantBuffer m_SSAO_CB[FrameCount];
	ConstantBuffer m_TonemapCB[FrameCount];
	ConstantBuffer m_DirectionalLightCB[FrameCount];
	ConstantBuffer m_PointLightCB[NUM_POINT_LIGHTS];
	ConstantBuffer m_SpotLightCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_CameraCB[FrameCount];
	ConstantBuffer m_DirLightShadowMapTransformCB[FrameCount];
	ConstantBuffer m_SpotLightShadowMapTransformCB[NUM_SPOT_LIGHTS];
	ConstantBuffer m_TransformCB[FrameCount];
	ConstantBuffer m_MeshCB;
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

	virtual bool OnInit() override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual void OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;
	void ChangeDisplayMode(bool hdr);
	void DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward);
	void DrawDirectionalLightShadowMap(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward);
	void DrawSpotLightShadowMap(ID3D12GraphicsCommandList* pCmdList, uint32_t spotLightIdx);
	void DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode);
	void DrawSSAO(ID3D12GraphicsCommandList* pCmdList);
	void DrawAmbientLight(ID3D12GraphicsCommandList* pCmdList);
	void DrawTonemap(ID3D12GraphicsCommandList* pCmdList);
};
