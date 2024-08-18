#include "SkyCommon.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
")"\

RWTexture2D<float3> OutResult : register(u0);

static const float MULTI_SCATTERING_SAMPLE_COUNT = 15.0f; // referenced UE.

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 pixPos = DTid + 0.5f;
	float2 uv = pixPos / float2(MultiScatteringLUT_Width, MultiScatteringLUT_Height);
	// We do no apply UV transform from sub range here as it has minimal impact.

	float cosLightZenithAngle = uv.x * 2.0f - 1.0f;
	float3 lightDir = float3(0.0f, sqrt(1.0f - cosLightZenithAngle * cosLightZenithAngle), cosLightZenithAngle);
	const float3 nullLightDirection = float3(0.0f, 0.0f, 1.0f);
	const float3 nullLightIlluminance = float3(0.0f, 0.0f, 0.0f);
	const float3 oneIlluminance = float3(1.0f, 1.0f, 1.0f);
	float viewHeight = bottomRadiusKm + uv.y * (topRadiusKm - bottomRadiusKm);

	float3 worldPos = float3(0.0f, 0.0f, viewHeight);
	float3 worldDir = float3(0.0f, 0.0f, 1.0f);

	SamplingSetup sampling = (SamplingSetup)0;
	{
		sampling.variableSampleCount = false;
		sampling.sampleCountIni = MULTI_SCATTERING_SAMPLE_COUNT;
	}

	// TODO:impl
	const bool ground = true;
	const bool mieRayPhase = false;

	OutResult[pixPos] = float3(0, 0, 0);
}