#pragma once

#include <SimpleMath.h>
#include "App.h"
#include "VertexBuffer.h"
#include "ConstantBuffer.h"
#include "StructuredBuffer.h"
#include "ColorTarget.h"
#include "DepthTarget.h"
#include "RootSignature.h"
#include "Texture.h"
#include "Camera.h"

class ParticleSampleApp : public App
{
public:
	ParticleSampleApp(uint32_t width, uint32_t height);
	virtual ~ParticleSampleApp();

private:
	Camera m_Camera;
	int m_PrevCursorX = 0;
	int m_PrevCursorY = 0;
	ComPtr<ID3D12PipelineState> m_pDrawParticlesPSO;
	RootSignature m_DrawParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pBackBufferPSO;
	RootSignature m_BackBufferRootSig;
	VertexBuffer m_QuadVB;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_DrawParticlesTarget;
	ConstantBuffer m_CameraCB[FRAME_COUNT];
	StructuredBuffer m_ParticlesSB;
	ConstantBuffer m_BackBufferCB;

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;

	void UpdateParticles(ID3D12GraphicsCommandList* pCmdList);
	void DrawParticles(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProj);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
