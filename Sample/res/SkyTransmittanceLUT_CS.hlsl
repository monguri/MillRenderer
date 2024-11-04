#include "SkyCommon.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(UAV(u0))"\

RWTexture2D<float3> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 pixPos = DTid + 0.5f;
	float2 uv = pixPos / float2(TransmittanceLUT_Width, TransmittanceLUT_Height);

	float viewHeight;
	float viewZenithCosAngle;

	UVtoLUTTransmittanceParams(viewHeight, viewZenithCosAngle, bottomRadiusKm, topRadiusKm, uv);

	float3 worldPos = float3(0.0f, 0.0f, viewHeight);
	float3 worldDir = float3(0.0f, sqrt(1.0f - viewZenithCosAngle * viewZenithCosAngle), viewZenithCosAngle);

	SamplingSetup sampling = (SamplingSetup)0;
	{
		sampling.variableSampleCount = false;
		sampling.sampleCountIni = 10.0f;
	}

	const bool ground = false;
	const bool mieRayPhase = false;
	const float3 nullLightDirection = float3(0.0f, 0.0f, 1.0f);
	const float3 nullLightIlluminance = float3(0.0f, 0.0f, 0.0f);

	SingleScatteringResult ss = IntegrateSingleScatteredLuminance(
		worldPos, worldDir,
		ground, sampling, mieRayPhase,
		nullLightDirection, nullLightIlluminance);
	float3 transmittance = exp(-ss.opticalDepth);

	OutResult[pixPos] = transmittance;
}