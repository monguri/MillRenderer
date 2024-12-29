#include "Common.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\
", StaticSampler"\
"("\
"s1"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

#define SAMPLESET_ARRAY_SIZE_HALF_RES 6
static const float2 OcclusionSamplesOffsetsHalfRes[SAMPLESET_ARRAY_SIZE_HALF_RES ]=
{
	// 6 points distributed on the unit disc, spiral order and distance
	float2(0.000, 0.200), 
	float2(0.325, 0.101), 
	float2(0.272, -0.396), 
	float2(-0.385, -0.488), 
	float2(-0.711, 0.274), 
	float2(0.060, 0.900) 
};

#define SAMPLESET_ARRAY_SIZE_FULL_RES 3
static const float2 OcclusionSamplesOffsetsFullRes[SAMPLESET_ARRAY_SIZE_FULL_RES] =
{
	// 3 points distributed on the unit disc, spiral order and distance
	float2(0, -1.0f) * 0.43f, 
	float2(0.58f, 0.814f) * 0.7f, 
	float2(-0.58f, 0.814f) 
};

#define SAMPLE_STEPS 2

static const float FLOAT16F_SCALE = 4096.0f * 32.0f; // referred UE // TODO: share it with SSAO_PS.hlsl
static const float THRESHOLD_INVERSE = 0.3f; // refered UE.
static const float AO_RADIUS_IN_VS = 0.5f;
static const float AO_BIAS = 0.005f;
static const float AO_CONTRAST_HALF_RES = 1.0f;
static const float AO_INTENSITY_HALF_RES = 0.5f;
static const float AO_MIP_BLEND = 0.6f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSAO : register(b0)
{
	float4x4 ViewMatrix : packoffset(c0);
	float4x4 InvProjMatrix : packoffset(c4);
	int Width : packoffset(c8);
	int Height : packoffset(c8.y);
	float2 RandomationSize : packoffset(c8.z);
	float2 TemporalOffset : packoffset(c9);
	float Near : packoffset(c9.z);
	float Far : packoffset(c9.w);
	float InvTanHalfFov : packoffset(c10);
	int bHalfRes : packoffset(c10.y);
	float Contrast  : packoffset(c10.z);
	float Intensity  : packoffset(c10.w);
}

Texture2D DepthMap : register(t0);
// When half resolution, it is the SSAO setup texture, when full resolution, it is the scene normal texture.
Texture2D SSAOSetupTex : register(t1);
SamplerState PointClampSmp : register(s0);

Texture2D RandomNormalTex : register(t2);
SamplerState PointWrapSmp : register(s1);

Texture2D SSAOHalfRes : register(t3);
Texture2D NormalMap : register(t4);

float ConvertViewZtoDeviceZ(float viewZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ;
}

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

float3 ConverFromNDCToVS(float4 ndcPos)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float viewPosZ = ConvertFromDeviceZtoViewZ(deviceZ);
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 viewPos = mul(InvProjMatrix, clipPos);
	
	return viewPos.xyz;
}

float GetDeviceZ(float2 uv)
{
	if (bHalfRes)
	{
		float viewZ = -SSAOSetupTex.Sample(PointClampSmp, uv).w * FLOAT16F_SCALE;
		return ConvertViewZtoDeviceZ(viewZ);
	}
	else
	{
		return DepthMap.Sample(PointClampSmp, uv).r;
	}
}

float3 GetWSNormal(float2 uv)
{
	if (bHalfRes)
	{
		return normalize(SSAOSetupTex.Sample(PointClampSmp, uv).xyz * 2.0f - 1.0f);
	}
	else
	{
		return normalize(NormalMap.Sample(PointClampSmp, uv).xyz * 2.0f - 1.0f);
	}
}

float2 WedgeWithNormal(float2 screenPos, float2 localRandom, float3 viewPos, float3 viewNormal)
{
	float2 screenPosL = screenPos + localRandom;
	float2 screenPosR = screenPos - localRandom;

	float deviceZ_L = DepthMap.Sample(PointClampSmp, screenPosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r;
	float deviceZ_R = DepthMap.Sample(PointClampSmp, screenPosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r;

	float4 ndcPosL = float4(screenPosL, deviceZ_L, 1);
	float4 ndcPosR = float4(screenPosR, deviceZ_R, 1);
	float3 viewPosL = ConverFromNDCToVS(ndcPosL);
	float3 viewPosR = ConverFromNDCToVS(ndcPosR);

	float3 deltaL = (viewPosL - viewPos);
	float3 deltaR = (viewPosR - viewPos);

	float invNormalAngleL = max((dot(deltaL, viewNormal) + viewPos.z * AO_BIAS) / dot(deltaL, deltaL), 0);
	float invNormalAngleR = max((dot(deltaR, viewNormal) + viewPos.z * AO_BIAS) / dot(deltaR, deltaR), 0);

	return float2(invNormalAngleL, invNormalAngleR);
}

//TODO: common with SSAOSetup_PS.hlsl
// 0: not similar .. 1:very similar
float ComputeDepthSimilarity(float depthA, float depthB, float tweakScale)
{
	return saturate(1 - abs(depthA - depthB) * tweakScale);
}

float ComputeUpsampleContribution(float sceneDepth, float2 inUV, float3 centerWorldNormal)
{
	const int SAMPLE_COUNT = 9;
	float2 uv[SAMPLE_COUNT];

	float2 SSAO_DownsampledAOInverseSize = float2(1.0f / (Width * 0.5f), 1.0f / (Height * 0.5f));

	uv[0] = inUV + float2(-1, -1) * SSAO_DownsampledAOInverseSize;
	uv[1] = inUV + float2(0, -1) * SSAO_DownsampledAOInverseSize;
	uv[2] = inUV + float2(1, -1) * SSAO_DownsampledAOInverseSize;
	uv[3] = inUV + float2(-1, 0) * SSAO_DownsampledAOInverseSize;
	uv[4] = inUV + float2(0, 0) * SSAO_DownsampledAOInverseSize;
	uv[5] = inUV + float2(1, 0) * SSAO_DownsampledAOInverseSize;
	uv[6] = inUV + float2(-1, 1) * SSAO_DownsampledAOInverseSize;
	uv[7] = inUV + float2(0, 1) * SSAO_DownsampledAOInverseSize;
	uv[8] = inUV + float2(1, 1) * SSAO_DownsampledAOInverseSize;

	const float SMALL_VALUE = 0.0001f;

	// to avoid division by 0
	float weightSum = SMALL_VALUE;
	float ret = SMALL_VALUE;

	float minIteration = 1.0f;

	for (int i = 0; i < SAMPLE_COUNT; i++)
	{
		float sampleValue = SSAOHalfRes.Sample(PointClampSmp, uv[i]).r;

		float4 normalAndSampleDepth = SSAOSetupTex.Sample(PointClampSmp, uv[i]);
		float sampleDepth = normalAndSampleDepth.w * FLOAT16F_SCALE;

		// when tweaking this constant look for crawling pattern at edges
		float weight = ComputeDepthSimilarity(sampleDepth, sceneDepth, THRESHOLD_INVERSE);

		float3 localWorldNormal = normalize(normalAndSampleDepth.xyz * 2 - 1);
		weight *= saturate(dot(centerWorldNormal, localWorldNormal));

		// todo: 1 can be put into the input to save an instruction
		ret += sampleValue * weight;
		weightSum += weight;
	}

	ret /= weightSum;
	return ret;
}

// Referenced the paper "The alchemy screen-space ambient obscurance algorithm"
[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float deviceZ = GetDeviceZ(input.TexCoord);
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 viewPos = ConverFromNDCToVS(ndcPos);

	float3 worldNormal = GetWSNormal(input.TexCoord);
	float3 viewNormal = normalize(mul((float3x3)ViewMatrix, worldNormal));

	//// [-depth,depth]x[-depth,depth]x[near,far] i.e. view space pos.
	//float3 viewSpacePosition = ConverFromSSPosToVSPos(screenPos, sceneDepth);

	float viewZ = ConvertFromDeviceZtoViewZ(deviceZ);
	float invDepth = 1.0f / -viewZ;
	// under condition that aspect ratio is 1
	float AORadiusInAspectRatio1SS = AO_RADIUS_IN_VS * invDepth * InvTanHalfFov;
	float invAspectRatio = Height / (float)Width;
	float2 AORadiusInSS = float2(AORadiusInAspectRatio1SS * invAspectRatio, AORadiusInAspectRatio1SS);

#if 0 // test not to use random normal texture.
	float2 rotation = float2(0, 1);
#else
	float2 viewportUVtoRandomUV = float2(Width, Height) / RandomationSize;
	float2 rotation = (RandomNormalTex.Sample(PointWrapSmp, input.TexCoord * viewportUVtoRandomUV + TemporalOffset).rg * 2.0f - 1.0f);
#endif

	float accumulator = 0;
	int sampleSetArraySize = (bHalfRes ? SAMPLESET_ARRAY_SIZE_HALF_RES : SAMPLESET_ARRAY_SIZE_FULL_RES);

	// disk random loop
	for (int i = 0; i < sampleSetArraySize; i++)
	{
		float2 unrotatedRandom = (bHalfRes ? OcclusionSamplesOffsetsHalfRes[i] : OcclusionSamplesOffsetsFullRes[i]);
		float2 localRandom = (unrotatedRandom.x * rotation + unrotatedRandom.y * float2(-rotation.y, rotation.x)) * AORadiusInSS;

		float2 localAccumulator = 0;

		// ray-march loop
		for (uint step = 0; step < SAMPLE_STEPS; step++)
		{
			float scale = (step + 1) / (float)SAMPLE_STEPS;

			float2 stepSample = WedgeWithNormal(screenPos, scale * localRandom, viewPos, viewNormal);
			localAccumulator = max(localAccumulator, stepSample);
		}

		accumulator += localAccumulator.x + localAccumulator.y;
	}

	float numSample = sampleSetArraySize * 2;
	float result = max(1 - accumulator / numSample * 2, 0.0f);

	if (!bHalfRes)
	{
		float halfResAOFiltered = ComputeUpsampleContribution(-viewZ, input.TexCoord, worldNormal);
		// recombined result from multiple resolutions
		result = lerp(result, halfResAOFiltered, AO_MIP_BLEND);
	}

	if (bHalfRes)
	{
		result = 1 - (1 - pow(result, AO_CONTRAST_HALF_RES)) * AO_INTENSITY_HALF_RES;
	}
	else
	{
		// abs is required to avoid pow() warning.
		result = 1 - (1 - pow(abs(result), Contrast)) * Intensity;
	}

	return float4(result, result, result, 1);
}