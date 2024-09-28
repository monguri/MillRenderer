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

class ParticleSampleApp : public App
{
public:
	ParticleSampleApp(uint32_t width, uint32_t height);
	virtual ~ParticleSampleApp();

private:
	virtual bool OnInit(HWND hWnd) override;
	virtual void OnTerm() override;
	virtual void OnRender() override;
	virtual bool OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) override;
};
