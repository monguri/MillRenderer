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

static const float CM_TO_KM = 0.00001f;

cbuffer CbCamera
{
	float3 CameraPosition : packoffset(c0);
};

Texture2D MultiScatteredLuminaceLutTexture : register(t1);
RWTexture2D<float3> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 pixPos = DTid + 0.5f;
	float2 uv = pixPos / float2(ViewLUT_Width, ViewLUT_Height);

	float3 worldPos = CameraPosition * CM_TO_KM - float3(0, -BottomRadiusKm, 0);

	// For the sky view lut to work, and not be distorted, we need to transform the view and light directions 
	// into a referential with UP being perpendicular to the ground. And with origin at the planet center.
	
	// This is the local referencial
	float3x3 localReferencial = (float3x3)SkyViewLutReferential;

	// This is the LUT camera height and position in the local referential
	float viewHeight = length(worldPos);
	worldPos = float3(0, viewHeight, 0);

	// Get the view direction in this local referential
	float3 worldDir;
	UvToSkyViewLutParams(worldDir, viewHeight, uv);
	// And also both light source direction
	float3 atmosphereLightDirection = AtmosphereLightDirection;
	atmosphereLightDirection = mul(localReferencial, atmosphereLightDirection);


	OutResult[int2(pixPos)] = MultiScatteredLuminaceLutTexture.SampleLevel(LinearClampSampler, uv, 0).rgb;
}