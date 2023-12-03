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

#define SAMPLE_STEP 2

static const float AORadiusInShader = 0.125f;
static const float ScaleRadiusInWorldSpace = 0.125f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSAO : register(b0)
{
	int Width;
	int Height;
	float Near;
	float Far;
	float InvTanHalfFov;
}

Texture2D DepthMap : register(t0);
SamplerState DepthSmp : register(s0);

float ConvertFromDeviceZtoLinearZ(float deviceZ)
{
	return deviceZ * (Far - Near) + Near;
}

float3 ReconstructCSPos(float sceneDepth, float2 screenPos)
{
	return float3(screenPos * sceneDepth, sceneDepth);
}

float2 WedgeNoNormal(float2 screenSpacePosCenter, float2 localRandom, float3 invFovFix, float3 viewSpacePosition)
{
	float2 screenSpacePosL = screenSpacePosCenter + localRandom;
	float2 screenSpacePosR = screenSpacePosCenter - localRandom;

	float absL = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(DepthSmp, screenSpacePosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);
	float absR = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(DepthSmp, screenSpacePosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);

	float3 samplePositionL = ReconstructCSPos(absL, screenSpacePosL);
	float3 samplePositionR = ReconstructCSPos(absR, screenSpacePosR);

	float3 deltaL = (samplePositionL - viewSpacePosition) * invFovFix;
	float3 deltaR = (samplePositionR - viewSpacePosition) * invFovFix;

	float flatSurfaceBias = 0.05f;

	float left = viewSpacePosition.z - absL;
	float right = viewSpacePosition.z - absR;

	float normAngle = acos(dot(deltaL, deltaR) / sqrt(dot(deltaL, deltaL) * dot(deltaR, deltaR))) / F_PI;

	if (left + right < flatSurfaceBias)
	{
		normAngle = 1;
	}

	float weight = 1.0f;

	const float invAmbientOcclusionDistance = 1.0f / 0.8f;
	float viewDepthAdd = 1.0f - viewSpacePosition.z * invAmbientOcclusionDistance;

	weight *= saturate(samplePositionL.z * invAmbientOcclusionDistance + viewDepthAdd);
	weight *= saturate(samplePositionR.z * invAmbientOcclusionDistance + viewDepthAdd);

	return float2((1 - normAngle) / (weight + 0.001f), weight);
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 fovFix = float3(InvTanHalfFov, InvTanHalfFov * Width / Height, 1.0f);
	float3 invFovFix = 1.0f / fovFix;

	float deviceZ = DepthMap.Sample(DepthSmp, input.TexCoord).r;
	float sceneDepth = ConvertFromDeviceZtoLinearZ(deviceZ);

	// [-1,1]x[-1,1]
	float2 screenSpacePos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	// [-depth,depth]x[-depth,depth]x[near,far] i.e. view space pos.
	float3 viewSpacePosition = ReconstructCSPos(sceneDepth, screenSpacePos);

	float actualAORadius = AORadiusInShader * sceneDepth;

	float2 randomVec = float2(0, 1) * actualAORadius;

	float2 fovFixXY = fovFix.xy * (1.0f / viewSpacePosition.z);
	float4 randomBase = float4(randomVec, -randomVec.y, randomVec.x) * float4(fovFixXY, fovFixXY);

	float2 weightAccumulator = 0.0001f;

	for (int i = 0; i < SAMPLESET_ARRAY_SIZE; i++)
	{
		float2 unrotatedRandom = OcclusionSamplesOffsets[i];
		float2 localRandom = (unrotatedRandom.x * randomBase.xy + unrotatedRandom.y * randomBase.zw);

		float2 localAllumulator = 0.0f;

		for (uint step = 0; step < SAMPLE_STEP; step++)
		{
			float scale = (step + 1) / SAMPLE_STEP;

			float2 stepSample = WedgeNoNormal(screenSpacePos, localRandom, invFovFix, viewSpacePosition);
			localAllumulator = lerp(localAllumulator, float2(max(localAllumulator.x, stepSample.x), 1), stepSample.y);
		}

		weightAccumulator += float2((1 - localAllumulator.x) * (1 - localAllumulator.x) * localAllumulator.y, localAllumulator.y);
	}

	return float4(weightAccumulator.x / weightAccumulator.y, 0, 0, 1);
}