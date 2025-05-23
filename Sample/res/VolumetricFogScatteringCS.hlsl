#include "Common.hlsli"

#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
#define SINGLE_SAMPLE_SHADOW_MAP
#include "ShadowMap.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(CBV(b1))"\
", DescriptorTable(CBV(b2))"\
", DescriptorTable(CBV(b3))"\
", DescriptorTable(CBV(b4))"\
", DescriptorTable(CBV(b5))"\
", DescriptorTable(CBV(b6))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(SRV(t1))"\
", DescriptorTable(SRV(t2))"\
", DescriptorTable(SRV(t3))"\
", DescriptorTable(SRV(t4))"\
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
", StaticSampler"\
"("\
"s1"\
", filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_LESS_EQUAL"\
", borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE"\
")"\

#ifndef MIN_DIST
#define MIN_DIST (0.01)
#endif // MIN_DIST

// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XYZ = 4;

static const float INVERSE_SQUARED_LIGHT_DISTANCE_BIAS_SCALE = 1.0f; // refered UE
static const float SCATTERING_DISTRIBUTION = 0.2f; // refered UE
static const float HISTORY_WEIGHT = 0.9; // refered UE

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	float4x4 ClipToPrevClip : packoffset(c4);
	int3 GridSize : packoffset(c8);
	float Near : packoffset(c8.w);
	float Far : packoffset(c9);
	float3 FrameJitterOffsetValue : packoffset(c9.y);
	float DirectionalLightScatteringIntensity : packoffset(c10);
	float SpotLightScatteringIntensity : packoffset(c10.y);
}

cbuffer CbDirectionalLight : register(b1)
{
	float3 DirLightColor : packoffset(c0);
	float3 DirLightForward : packoffset(c1);
	float2 DirLightShadowMapSize : packoffset(c2); // x is pixel size, y is texel size on UV.
};

cbuffer CbSpotLight1 : register(b2)
{
	float3 SpotLight1Position : packoffset(c0);
	float SpotLight1InvSqrRadius : packoffset(c0.w);
	float3 SpotLight1Color : packoffset(c1);
	float SpotLight1Intensity : packoffset(c1.w);
	float3 SpotLight1Forward : packoffset(c2);
	float SpotLight1AngleScale : packoffset(c2.w);
	float SpotLight1AngleOffset : packoffset(c3);
	int SpotLight1Type : packoffset(c3.y);
	float2 SpotLight1ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

cbuffer CbSpotLight2 : register(b3)
{
	float3 SpotLight2Position : packoffset(c0);
	float SpotLight2InvSqrRadius : packoffset(c0.w);
	float3 SpotLight2Color : packoffset(c1);
	float SpotLight2Intensity : packoffset(c1.w);
	float3 SpotLight2Forward : packoffset(c2);
	float SpotLight2AngleScale : packoffset(c2.w);
	float SpotLight2AngleOffset : packoffset(c3);
	int SpotLight2Type : packoffset(c3.y);
	float2 SpotLight2ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

cbuffer CbSpotLight3 : register(b4)
{
	float3 SpotLight3Position : packoffset(c0);
	float SpotLight3InvSqrRadius : packoffset(c0.w);
	float3 SpotLight3Color : packoffset(c1);
	float SpotLight3Intensity : packoffset(c1.w);
	float3 SpotLight3Forward : packoffset(c2);
	float SpotLight3AngleScale : packoffset(c2.w);
	float SpotLight3AngleOffset : packoffset(c3);
	int SpotLight3Type : packoffset(c3.y);
	float2 SpotLight3ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

cbuffer CbCamera : register(b5)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbTransform : register(b6)
{
	float4x4 ViewProj : packoffset(c0);
	float4x4 WorldToDirLightShadowMap : packoffset(c4);
	float4x4 WorldToSpotLight1ShadowMap : packoffset(c8);
	float4x4 WorldToSpotLight2ShadowMap : packoffset(c12);
	float4x4 WorldToSpotLight3ShadowMap : packoffset(c16);
}

Texture3D HistoryMap : register(t0);
Texture2D DirLightShadowMap : register(t1);
Texture2D SpotLight1ShadowMap : register(t2);
Texture2D SpotLight2ShadowMap : register(t3);
Texture2D SpotLight3ShadowMap : register(t4);

SamplerState LinearClampSmp : register(s0);
SamplerComparisonState ShadowSmp : register(s1);

RWTexture3D<float4> OutResult : register(u0);

float ConvertViewZtoVolumetricFogDeviceZ(float viewZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ;
}

float ConvertFromVolumetricFogDeviceZtoViewZ(float deviceZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

float3 ConverFromNDCToCameraOriginWS(float4 ndcPos, float viewPosZ)
{
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 cameraOriginWorldPos = mul(InvVRotPMatrix, clipPos);
	
	return cameraOriginWorldPos.xyz;
}

float3 ComputeCellCameraOriginWorldPosition(float3 gridCoordinate, float3 cellOffset)
{
	float2 uv = (gridCoordinate.xy + cellOffset.xy) / GridSize.xy;
	// TODO: exp slice
	float linearDepth = lerp(Near, Far, (gridCoordinate.z + cellOffset.z) / float(GridSize.z));
	float viewPosZ = -linearDepth;
	float deviceZ = ConvertViewZtoVolumetricFogDeviceZ(viewPosZ);
	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos, viewPosZ);
	return cameraOriginWorldPos;
}

float3 ComputePrevUVWfromCurrentGridCoord(float3 gridCoordinate, float3 cellOffset)
{
	// TODO: double calcuration as ComputeCellCameraOriginWorldPosition() call.

	float2 uv = (gridCoordinate.xy + cellOffset.xy) / GridSize.xy;
	// TODO: exp slice
	float linearDepth = lerp(Near, Far, (gridCoordinate.z + cellOffset.z) / float(GridSize.z));
	float viewPosZ = -linearDepth;
	float deviceZ = ConvertViewZtoVolumetricFogDeviceZ(viewPosZ);
	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);

	float4 prevClipPos = mul(ClipToPrevClip, ndcPos);
	float4 prevNdcPos = prevClipPos / prevClipPos.w;
	float2 prevUV = (prevNdcPos.xy + float2(1, -1)) * float2(0.5, -0.5);
	float prevDeviceZ = prevNdcPos.z;
	float prevLinearDepth = -ConvertFromVolumetricFogDeviceZtoViewZ(prevDeviceZ);
	// TODO: exp slice
	float prevW = (prevLinearDepth - Near) / (Far - Near);
	return float3(prevUV, prevW);
}

// Positive g = forward scattering
// Zero g = isotropic
// Negative g = backward scattering
// Follows PBRT convention http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html#PhaseHG
float HenyeyGreensteinPhase(float G, float CosTheta)
{
	// Reference implementation (i.e. not schlick approximation). 
	// See http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
	float Numer = 1.0f - G * G;
	float Denom = 1.0f + G * G + 2.0f * G * CosTheta;
	return Numer / (4.0f * F_PI * Denom * sqrt(Denom));
}

float luminance(float3 linearColor)
{
	return dot(linearColor, float3(0.3f, 0.59f, 0.11f));
}

// TODO: same code for SponzaPS.hlsli
float GetAngleAttenuation
(
	float3 normalizedLightVector,
	float3 lightDir,
	float lightAngleScale,
	float lightAngleOffset
)
{
	float cd = dot(lightDir, normalizedLightVector);
	float attenuation = saturate(cd * lightAngleScale + lightAngleOffset);
	attenuation *= attenuation;
	return attenuation;
}

// TODO: almost same code for SponzaPS.hlsli. difference is only outLightDirection argument.
float3 EvaluateSpotLight
(
	float3 worldPos,
	float3 lightPos,
	float distBiasSq,
	float lightInvRadiusSq,
	float3 lightForward,
	float3 lightColor,
	float lightAngleScale,
	float lightAngleOffset,
	float3 outLightDirection
)
{
	float3 unnormalizedLightVector = lightPos - worldPos;
	float sqrDist = dot(unnormalizedLightVector, unnormalizedLightVector);
	float att = 1.0f / max(sqrDist + distBiasSq, MIN_DIST * MIN_DIST);
	float3 L = normalize(unnormalizedLightVector);
	outLightDirection = -L;
	att *= GetAngleAttenuation(L, -lightForward, lightAngleScale, lightAngleOffset);
	return lightColor * att / F_PI;
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 gridCoordinate = DTid;

	float3 cameraOriginWorldPos = ComputeCellCameraOriginWorldPosition(gridCoordinate, FrameJitterOffsetValue);
	float3 cameraVector = normalize(cameraOriginWorldPos);
	// TODO: do with camera origin WS.
	float3 worldPos = CameraPosition + cameraOriginWorldPos;

	float3 lightScattering = 0;

	float4 dirLightShadowPos = mul(WorldToDirLightShadowMap, float4(worldPos, 1));
	// dividing by w is not necessary because it is 1 by orthogonal.
	float3 dirLightShadowCoord = dirLightShadowPos.xyz / dirLightShadowPos.w;
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, ShadowSmp, DirLightShadowMapSize, dirLightShadowCoord, 0);

	lightScattering += DirLightColor * dirLightShadowMult * DirectionalLightScatteringIntensity * HenyeyGreensteinPhase(SCATTERING_DISTRIBUTION, dot(-DirLightForward, -cameraVector));

	float3 cameraOriginNextCellWorldPos = ComputeCellCameraOriginWorldPosition(gridCoordinate + 1, FrameJitterOffsetValue);
	float cellRadius = length(cameraOriginNextCellWorldPos - cameraOriginWorldPos);
	float distBiasSq = max(cellRadius * INVERSE_SQUARED_LIGHT_DISTANCE_BIAS_SCALE, 0.01f); //TODO: magic number 1cm, refered UE.
	distBiasSq *= distBiasSq;

	float3 spotLight1Dir = 0;
	float3 spotLight1 = EvaluateSpotLight
	(
		worldPos, // TODO: do with camera origin WS.
		SpotLight1Position,
		distBiasSq,
		SpotLight1InvSqrRadius,
		SpotLight1Forward,
		SpotLight1Color,
		SpotLight1AngleScale,
		SpotLight1AngleOffset,
		spotLight1Dir
	) * SpotLight1Intensity;

	float4 spotLight1ShadowPos = mul(WorldToSpotLight1ShadowMap, float4(worldPos, 1));
	// dividing by w is not necessary because it is 1 by orthogonal.
	float3 spotLight1ShadowCoord = spotLight1ShadowPos.xyz / spotLight1ShadowPos.w;
	float spotLight1ShadowMult = GetShadowMultiplier(SpotLight1ShadowMap, ShadowSmp, SpotLight1ShadowMapSize, spotLight1ShadowCoord, 0);

	lightScattering += spotLight1 * spotLight1ShadowMult * SpotLightScatteringIntensity 
						* HenyeyGreensteinPhase(SCATTERING_DISTRIBUTION, dot(-spotLight1Dir, -cameraVector));

	float3 spotLight2Dir = 0;
	float3 spotLight2 = EvaluateSpotLight
	(
		worldPos,
		SpotLight2Position,
		distBiasSq,
		SpotLight2InvSqrRadius,
		SpotLight2Forward,
		SpotLight2Color,
		SpotLight2AngleScale,
		SpotLight2AngleOffset,
		spotLight2Dir
	) * SpotLight2Intensity;

	float4 spotLight2ShadowPos = mul(WorldToSpotLight2ShadowMap, float4(worldPos, 1));
	// dividing by w is not necessary because it is 1 by orthogonal.
	float3 spotLight2ShadowCoord = spotLight2ShadowPos.xyz / spotLight2ShadowPos.w;
	float spotLight2ShadowMult = GetShadowMultiplier(SpotLight2ShadowMap, ShadowSmp, SpotLight2ShadowMapSize, spotLight2ShadowCoord, 0);

	lightScattering += spotLight2 * spotLight2ShadowMult * SpotLightScatteringIntensity 
						* HenyeyGreensteinPhase(SCATTERING_DISTRIBUTION, dot(-spotLight2Dir, -cameraVector));

	float3 spotLight3Dir = 0;
	float3 spotLight3 = EvaluateSpotLight
	(
		worldPos,
		SpotLight3Position,
		distBiasSq,
		SpotLight3InvSqrRadius,
		SpotLight3Forward,
		SpotLight3Color,
		SpotLight3AngleScale,
		SpotLight3AngleOffset,
		spotLight3Dir
	) * SpotLight3Intensity;

	float4 spotLight3ShadowPos = mul(WorldToSpotLight3ShadowMap, float4(worldPos, 1));
	// dividing by w is not necessary because it is 1 by orthogonal.
	float3 spotLight3ShadowCoord = spotLight3ShadowPos.xyz / spotLight3ShadowPos.w;
	float spotLight3ShadowMult = GetShadowMultiplier(SpotLight3ShadowMap, ShadowSmp, SpotLight3ShadowMapSize, spotLight3ShadowCoord, 0);

	lightScattering += spotLight3 * spotLight3ShadowMult * SpotLightScatteringIntensity 
						* HenyeyGreensteinPhase(SCATTERING_DISTRIBUTION, dot(-spotLight3Dir, -cameraVector));

	// TODO:
	float4 materialScatteringAndAbsorption = float4(1e-05f, 1e-05f, 1e-05f, 0);
	float extinction = materialScatteringAndAbsorption.w + luminance(materialScatteringAndAbsorption.rgb);

	float4 preExposedScatteringAndExtinction = float4(lightScattering * materialScatteringAndAbsorption.xyz, extinction);

	float3 prevUVW = ComputePrevUVWfromCurrentGridCoord(gridCoordinate, 0.5f);
	float4 preExposedHistoryScatteringAndExtinction = HistoryMap.SampleLevel(LinearClampSmp, prevUVW, 0);
	preExposedScatteringAndExtinction = lerp(preExposedScatteringAndExtinction, preExposedHistoryScatteringAndExtinction, HISTORY_WEIGHT);

	OutResult[gridCoordinate] = preExposedScatteringAndExtinction;
}