#include "SkyCommon.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(CBV(b1))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(SRV(t1))"\
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

static const float M_TO_KM = 0.001f;

cbuffer CbCamera
{
	float3 CameraPosition : packoffset(c0);
};

RWTexture2D<float3> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

static const float FAST_SKY_SAMPLE_COUNT_MIN = 4;
static const float FAST_SKY_SAMPLE_COUNT_MAX = 32;
static const float FAST_SKY_DISTANCE_TO_SAMPLE_COUNT_MAX_INV = 1.0f / 150;

float2 FromSubUvsToUnit(float2 uv, float2 size, float2 invSize)
{
	// [0.5, size - 1 + 0.5] / sizeだったUVを[0,1]に分布させる
	return (uv - 0.5f * invSize) * size / (size - 1.0f);
}

// SkyViewLut is a new texture used for fast sky rendering.
// It is low resolution of the sky rendering around the camera,
// basically a lat/long parameterisation with more texel close to the horizon for more accuracy during sun set.
void UvToSkyViewLutParams(out float3 viewDir, in float viewHeight, in float2 uv)
{
	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
	float2 size = float2(ViewLUT_Width, ViewLUT_Height);
	uv = FromSubUvsToUnit(uv, size, 1 / size);

	float vHorizon = sqrt(viewHeight * viewHeight - BottomRadiusKm * BottomRadiusKm);
	float cosBeta = vHorizon / viewHeight;

	float beta = acos(cosBeta);
	float zenithHorizonAngle = F_PI - beta;
	
	float viewZenithAngle;
	if (uv.y < 0.5f)
	{
		float coord = 2 * uv.y;
		coord = 1.0f - coord;
		coord *= coord;
		coord = 1.0f - coord;
		viewZenithAngle = zenithHorizonAngle * coord;
	}
	else
	{
		float coord = 2 * uv.y - 1;
		coord *= coord;
		viewZenithAngle = zenithHorizonAngle + beta * coord;
	}

	float cosViewZenithAngle = cos(viewZenithAngle);
	float sinViewZenithAngle = sqrt(1 - cosViewZenithAngle * cosViewZenithAngle) * (viewZenithAngle > 0.0f ? 1.0f : -1.0f); // Equivalent to sin(viewZenithAngle)

	float longitudeViewCosAngle = uv.x * 2 * F_PI;

	// Make sure those values are in range as it could disrupt other math done later such as sqrt(1.0-c*c)
	float cosLongitudeViewCosAngle = cos(longitudeViewCosAngle);
	float sinLongitudeViewCosAngle = sqrt(1 - cosLongitudeViewCosAngle * cosLongitudeViewCosAngle) * (longitudeViewCosAngle <= F_PI ? 1.0f : -1.0f); // Equivalent to sin(longitudeViewCosAngle)

	viewDir = float3(
		sinViewZenithAngle * sinLongitudeViewCosAngle,
		cosViewZenithAngle,
		sinViewZenithAngle * cosLongitudeViewCosAngle
	);
}

bool MoveToTopAtmosphere(inout float3 worldPos, in float3 worldDir, in float atmosphereTopRadius)
{
	float viewHeight = length(worldPos);
	if (viewHeight > atmosphereTopRadius)
	{
		float tTop = RaySphereIntersectNearest(worldPos, worldDir, float3(0, 0, 0), atmosphereTopRadius);
		if (tTop >= 0)
		{
			float3 upVector = worldPos / viewHeight;
			float3 upOffset = upVector * -PLANET_RADIUS_OFFSET;
			worldPos = worldPos + worldDir * tTop + upOffset;
		}
		else
		{
			// Ray is not intersecting the atmosphere
			return false;
		}
	}
	return true; // ok to start tracing
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 pixPos = DTid + 0.5f;
	float2 uv = pixPos / float2(ViewLUT_Width, ViewLUT_Height);

	// For the sky view lut to work, and not be distorted, we need to transform the view and light directions 
	// into a referential with UP being perpendicular to the ground. And with origin at the planet center.
	
	// This is the local referencial
	float3x3 localReferencial = (float3x3)SkyViewLutReferential;

	// This is the LUT camera height and position in the local referential
	float viewHeight = CameraPosition.y * M_TO_KM + BottomRadiusKm;
	float3 worldPos = float3(0, viewHeight, 0);

	// Get the view direction in this local referential
	float3 worldDir;
	UvToSkyViewLutParams(worldDir, viewHeight, uv);
	// And also both light source direction
	float3 atmosphereLightDirection = AtmosphereLightDirection;
	atmosphereLightDirection = mul(localReferencial, atmosphereLightDirection);

	// Move to top atmosphere as the starting point for ray marching.
	// This is critical to be after the above to not disrupt above atmosphere tests and voxel selection.
	if (!MoveToTopAtmosphere(worldPos, worldDir, TopRadiusKm))
	{
		// Ray is not intersecting the atmosphere
		OutResult[int2(pixPos)] = 0.0f;
		return;
	}

	SamplingSetup sampling = (SamplingSetup)0;
	{
		sampling.variableSampleCount = true;
		sampling.minSampleCount = FAST_SKY_SAMPLE_COUNT_MIN;
		sampling.maxSampleCount = FAST_SKY_SAMPLE_COUNT_MAX ;
		sampling.distanceToSampleCountMaxInv = FAST_SKY_DISTANCE_TO_SAMPLE_COUNT_MAX_INV;
	}

	const bool ground = false;
	const bool mieRayPhase = true;
	const bool whiteTransmittance = false;
	const bool multipleScatteringApproxSamplingEnabled = true;

	SingleScatteringResult ss = IntegrateSingleScatteredLuminance(
		worldPos, worldDir,
		ground, sampling, mieRayPhase,
		atmosphereLightDirection, AtmosphereLightIlluminanceOuterSpace, 
		whiteTransmittance, multipleScatteringApproxSamplingEnabled);

	OutResult[int2(pixPos)] = ss.L;
}