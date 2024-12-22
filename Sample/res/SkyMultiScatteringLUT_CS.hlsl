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
	float3 lightDir = float3(sqrt(1.0f - cosLightZenithAngle * cosLightZenithAngle), cosLightZenithAngle, 0.0f);
	const float3 oneLightIlluminance = float3(1.0f, 1.0f, 1.0f);
	float viewHeight = BottomRadiusKm + uv.y * (TopRadiusKm - BottomRadiusKm);

	float3 worldPos = float3(0.0f, viewHeight, 0.0f);
	float3 worldDir = float3(0.0f, 1.0f, 0.0f);

	SamplingSetup sampling = (SamplingSetup)0;
	{
		sampling.variableSampleCount = false;
		sampling.sampleCountIni = MULTI_SCATTERING_SAMPLE_COUNT;
	}

	const bool ground = true;
	const bool mieRayPhase = false;
	const bool whiteTransmittance = false;
	const bool multipleScatteringApproxSamplingEnabled = false;

	const float sphereSolidAngle = 4.0f * F_PI;
	const float isotropicPhase = 1.0f / sphereSolidAngle;

	SingleScatteringResult r0 = IntegrateSingleScatteredLuminance(
		worldPos, worldDir,
		ground, sampling, mieRayPhase,
		lightDir, oneLightIlluminance,
		whiteTransmittance, multipleScatteringApproxSamplingEnabled);
	SingleScatteringResult r1 = IntegrateSingleScatteredLuminance(
		worldPos, -worldDir,
		ground, sampling, mieRayPhase,
		lightDir, oneLightIlluminance,
		whiteTransmittance, multipleScatteringApproxSamplingEnabled);

	float3 integratedIlluminance = (sphereSolidAngle / 2.0f) * (r0.L + r1.L);
	float3 multiScatAs1 = (1.0f / 2.0f) * (r0.multiScatAs1 + r1.multiScatAs1);
	float3 inscatteredLuminance = integratedIlluminance * isotropicPhase;

	float3 multiScatAs1SQR = multiScatAs1 * multiScatAs1;
	float3 L = inscatteredLuminance * (1.0f + multiScatAs1 + multiScatAs1SQR + multiScatAs1 * multiScatAs1SQR * multiScatAs1SQR * multiScatAs1SQR);

	OutResult[int2(pixPos)] = L;
}