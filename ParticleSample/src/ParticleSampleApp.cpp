#include "ParticleSampleApp.h"

using namespace DirectX::SimpleMath;
ParticleSampleApp::ParticleSampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
{
}

ParticleSampleApp::~ParticleSampleApp()
{
}

bool ParticleSampleApp::OnInit(HWND hWnd)
{
	return true;
}

void ParticleSampleApp::OnTerm()
{
}

void ParticleSampleApp::OnRender()
{
}

bool ParticleSampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	return true;
}
