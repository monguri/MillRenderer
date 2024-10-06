#pragma once

#include <SimpleMath.h>
#include <chrono>
#include "App.h"
#include "VertexBuffer.h"
#include "ConstantBuffer.h"
#include "StructuredBuffer.h"
#include "ByteAddressBuffer.h"
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

	uint32_t m_NumSpawnPerFrame = 10;
	uint32_t m_InitialLife = 100;
	float m_InitialVelocityScale = 1.0f;

	std::chrono::high_resolution_clock::time_point m_PrevTime;
	ComPtr<ID3D12PipelineState> m_pResetNumParticlesPSO;
	RootSignature m_ResetNumParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pUpdateParticlesPSO;
	RootSignature m_UpdateParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pDrawParticlesPSO;
	RootSignature m_DrawParticlesRootSig;
	ComPtr<ID3D12CommandSignature> m_pDrawParticlesCommandSig;
	ComPtr<ID3D12PipelineState> m_pBackBufferPSO;
	RootSignature m_BackBufferRootSig;
	VertexBuffer m_QuadVB;
	DepthTarget m_SceneDepthTarget;
	ColorTarget m_DrawParticlesTarget;
	ConstantBuffer m_CameraCB[FRAME_COUNT];
	StructuredBuffer m_ParticlesSB[FRAME_COUNT];
	ByteAddressBuffer m_DrawParticlesIndirectArgsBB[FRAME_COUNT];
	ConstantBuffer m_SimulationCB;
	ConstantBuffer m_BackBufferCB;

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;

	void ResetNumParticles(ID3D12GraphicsCommandList* pCmdList, const ByteAddressBuffer& currDrawParticlesArgsBB);
	void UpdateParticles(ID3D12GraphicsCommandList* pCmdList, const StructuredBuffer& prevParticlesSB, const StructuredBuffer& currParticlesSB, const ByteAddressBuffer& prevDrawParticlesArgsBB, const ByteAddressBuffer& currDrawParticlesArgsBB);
	void DrawParticles(ID3D12GraphicsCommandList* pCmdList, const StructuredBuffer& currParticlesSB, const ByteAddressBuffer& currDrawParticlesArgsBB);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
