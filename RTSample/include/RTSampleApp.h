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
#include "ByteAddressBuffer.h"

class RTSampleApp : public App
{
public:
	RTSampleApp(uint32_t width, uint32_t height);
	virtual ~RTSampleApp();

private:
	TransformManipulator m_CameraManipulator;
	int m_PrevCursorX = 0;
	int m_PrevCursorY = 0;

	DepthTarget m_SceneDepthTarget;
	ConstantBuffer m_CameraCB[FRAME_COUNT];

	VertexBuffer m_TriangleVB;
	ByteAddressBuffer m_BlasScratchBB;
	ByteAddressBuffer m_BlasResultBB;
	ByteAddressBuffer m_TlasScratchBB;
	ByteAddressBuffer m_TlasResultBB;
	DescriptorHandle* m_pTlasResultSrvHandle = nullptr;
	ByteAddressBuffer m_TlasInstanceDescBB;
	RootSignature m_GlobalRootSig;

	ComPtr<ID3D12StateObject> m_pStateObject;
	ColorTarget m_RTTarget;
	ByteAddressBuffer m_ShaderTableBB;

	size_t m_ShaderTableEntrySize = 0;

	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;

	void RayTrace(ID3D12GraphicsCommandList4* pCmdList);
	void DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList);
	void DrawImGui(ID3D12GraphicsCommandList* pCmdList);
};
