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
	ComPtr<ID3D12PipelineState> m_pSceneOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneMaskPSO;
	ComPtr<ID3D12PipelineState> m_pSceneDepthOpaquePSO;
	ComPtr<ID3D12PipelineState> m_pSceneDepthMaskPSO;
	RootSignature m_SceneRootSig;
	ComPtr<ID3D12PipelineState> m_pTonemapPSO;
	RootSignature m_TonemapRootSig;
	DepthTarget m_ShadowMapTarget;
	ColorTarget m_SceneColorTarget;
	DepthTarget m_SceneDepthTarget;
	VertexBuffer m_QuadVB;
	ConstantBuffer m_TonemapCB[FrameCount];
	ConstantBuffer m_LightCB[FrameCount];
	ConstantBuffer m_CameraCB[FrameCount];
	ConstantBuffer m_TransformCB[FrameCount];
	ConstantBuffer m_MeshCB;
	std::vector<class Mesh*> m_pMesh;
	Material m_Material;
	float m_RotateAngle;
	int m_TonemapType;
	int m_ColorSpace;
	float m_BaseLuminance;
	float m_MaxLuminance;
	Texture m_SphereMap;
	DirectX::SimpleMath::Matrix m_View;
	DirectX::SimpleMath::Matrix m_Proj;
	Camera m_Camera;
	int m_PrevCursorX;
	int m_PrevCursorY;

	virtual bool OnInit() override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual void OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;
	void ChangeDisplayMode(bool hdr);
	void DrawScene(ID3D12GraphicsCommandList* pCmdList);
	void DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode);
	void DrawTonemap(ID3D12GraphicsCommandList* pCmdList);
};
