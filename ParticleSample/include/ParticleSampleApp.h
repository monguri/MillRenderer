#pragma once

#include <SimpleMath.h>
#include <chrono>
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
	std::chrono::high_resolution_clock::time_point m_PrevTime;
	ComPtr<ID3D12PipelineState> m_pUpdateParticlesPSO;
	RootSignature m_UpdateParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pDrawParticlesPSO;
	RootSignature m_DrawParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pBackBufferPSO;
	RootSignature m_BackBufferRootSig;
	VertexBuffer m_QuadVB;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_DrawParticlesTarget;
	ConstantBuffer m_CameraCB[FRAME_COUNT];
	StructuredBuffer m_ParticlesSB[FRAME_COUNT];
	ConstantBuffer m_TimeCB;
	ConstantBuffer m_BackBufferCB;

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;

	void UpdateParticles(ID3D12GraphicsCommandList* pCmdList);
	void DrawParticles(ID3D12GraphicsCommandList* pCmdList);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
