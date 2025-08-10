#pragma once

#include <SimpleMath.h>
#include <chrono>
#include "App.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ConstantBuffer.h"
#include "DepthTarget.h"
#include "RootSignature.h"
#include "TransformManipulator.h"

class RTSampleApp : public App
{
public:
	RTSampleApp(uint32_t width, uint32_t height);
	virtual ~RTSampleApp();

private:
	TransformManipulator m_CameraManipulator;
	int m_PrevCursorX = 0;
	int m_PrevCursorY = 0;

	ComPtr<ID3D12PipelineState> m_pUpdateParticlesPSO;
	RootSignature m_UpdateParticlesRootSig;
	ComPtr<ID3D12PipelineState> m_pBackBufferPSO;
	RootSignature m_BackBufferRootSig;
	VertexBuffer m_RTResultVB;
	IndexBuffer m_RTResultIB;
	DepthTarget m_SceneDepthTarget;
	ConstantBuffer m_CameraCB[FRAME_COUNT];

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;

	void TessellateQuad(ID3D12GraphicsCommandList* pCmdList);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
