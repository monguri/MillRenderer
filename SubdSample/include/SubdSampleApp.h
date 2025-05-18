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
#include "TransformManipulator.h"

class SubdSampleApp : public App
{
public:
	SubdSampleApp(uint32_t width, uint32_t height);
	virtual ~SubdSampleApp();
};
