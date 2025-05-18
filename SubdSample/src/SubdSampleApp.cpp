#include "SubdSampleApp.h"

#if 0 // TODO:
// imgui
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#endif

// DirectX libraries
#include <DirectXMath.h>
#include <CommonStates.h>
#include <DirectXHelpers.h>

// Framework
#include "FileUtil.h"
#include "Logger.h"
#include "ScopedTimer.h"

using namespace DirectX::SimpleMath;

SubdSampleApp::SubdSampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
{
}

SubdSampleApp::~SubdSampleApp()
{
}

