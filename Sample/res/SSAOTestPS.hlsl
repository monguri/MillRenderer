#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

#define SAMPLESET_ARRAY_SIZE 3
static const float2 OcclusionSamplesOffsets[SAMPLESET_ARRAY_SIZE] =
{
	// 3 points distributed on the unit disc, spiral order and distance
	float2(0, -1.0f) * 0.43f, 
	float2(0.58f, 0.814f) * 0.7f, 
	float2(-0.58f, 0.814f) 
};

#define SAMPLE_STEPS 2

static const float AO_RADIUS_IN_SS = 0.1f;
static const float AO_POWER = 2.0f;
static const float AO_INTENSITY = 0.5f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSAO : register(b0)
{
	float4x4 ViewMatrix;
	float4x4 InvViewProjMatrix;
	int Width;
	int Height;
	float2 RandomationSize;
	float2 TemporalOffset;
	float Near;
	float Far;
	float InvTanHalfFov;
	int bEnableSSAO;
}

Texture2D DepthMap : register(t0);
Texture2D NormalMap : register(t1);
SamplerState PointClampSmp : register(s0);

Texture2D RandomNormalTex : register(t2);
SamplerState PointWrapSmp : register(s1);

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) - Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

float3 ConverFromNDCToWS(float4 ndcPos)
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
	float4 worldPos = mul(InvViewProjMatrix, clipPos);
	
	return worldPos.xyz;
}

float3 WedgeWithNormal(float2 screenPos, float2 localRandom, float3 worldPos, float3 worldNormal)
{
	float2 screenPosL = screenPos + localRandom;
	float2 screenPosR = screenPos - localRandom;

	float deviceZ_L = DepthMap.Sample(PointClampSmp, screenPosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r;
	float deviceZ_R = DepthMap.Sample(PointClampSmp, screenPosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r;

	float4 ndcPosL = float4(screenPosL, deviceZ_L, 1);
	float4 ndcPosR = float4(screenPosR, deviceZ_R, 1);
	float3 worldPosL = ConverFromNDCToWS(ndcPosL);
	float3 worldPosR = ConverFromNDCToWS(ndcPosR);

	float3 deltaL = (worldPosL - worldPos);
	float3 deltaR = (worldPosR - worldPos);

	float invNormalAngleL = saturate(dot(deltaL, worldNormal) / dot(deltaL, deltaL));
	float invNormalAngleR = saturate(dot(deltaR, worldNormal) / dot(deltaR, deltaR));
	float weight = 1.0f;

	return float3(invNormalAngleL, invNormalAngleR, weight);
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 worldPos = ConverFromNDCToWS(ndcPos);

	float3 worldNormal = normalize(NormalMap.Sample(PointClampSmp, input.TexCoord).xyz * 2.0f - 1.0f);
	float3 viewSpaceNormal = normalize(mul((float3x3)ViewMatrix, worldNormal));

	//// [-depth,depth]x[-depth,depth]x[near,far] i.e. view space pos.
	//float3 viewSpacePosition = ConverFromSSPosToVSPos(screenPos, sceneDepth);

#if 1
	float2 rotation = float2(0, 1);
#else
	float2 rotation = (RandomNormalTex.Sample(PointWrapSmp, input.TexCoord * viewportUVtoRandomUV + TemporalOffset).rg * 2.0f - 1.0f);
#endif

	float2 weightAccumulator = 0.0001f;
	// disk random loop
	for (int i = 0; i < SAMPLESET_ARRAY_SIZE; i++)
	{
		float2 unrotatedRandom = OcclusionSamplesOffsets[i];
		float2 localRandom = (unrotatedRandom.x * rotation + unrotatedRandom.y * float2(-rotation.y, rotation.x)) * AO_RADIUS_IN_SS;

		float3 localAllumulator = 0.0f;

		// ray-march loop
		for (uint step = 0; step < SAMPLE_STEPS; step++)
		{
			float scale = (step + 1) / (float)SAMPLE_STEPS;

			float3 stepSample = WedgeWithNormal(screenPos, scale * localRandom, worldPos, worldNormal);
			localAllumulator = lerp(localAllumulator, float3(max(localAllumulator.xy, stepSample.xy), 1), stepSample.z);
		}

		weightAccumulator += float2((1 - localAllumulator.x) * (1 - localAllumulator.x) * localAllumulator.z, localAllumulator.z);
		weightAccumulator += float2((1 - localAllumulator.y) * (1 - localAllumulator.y) * localAllumulator.z, localAllumulator.z);
	}

	float result = weightAccumulator.x / weightAccumulator.y;
	// abs() to prevent shader warning
	result = 1 - (1 - pow(abs(result), AO_POWER)) * AO_INTENSITY;

	if (bEnableSSAO)
	{
		//float normalizedDepth = sceneDepth / 20; // 20 is manually adjustetd value
		//return float4(normalizedDepth, normalizedDepth, normalizedDepth, 1);

		return float4(result, result, result, 1);
	}
	else
	{
		return float4(1, 1, 1, 1);
	}
}