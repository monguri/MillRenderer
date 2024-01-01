#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

#define OPTIMIZATION_O1 0

#define USE_NORMALS 0
#define USE_NORMALS_MONGURI 1

#define SAMPLESET_ARRAY_SIZE 3
static const float2 OcclusionSamplesOffsets[SAMPLESET_ARRAY_SIZE] =
{
	// 3 points distributed on the unit disc, spiral order and distance
	float2(0, -1.0f) * 0.43f, 
	float2(0.58f, 0.814f) * 0.7f, 
	float2(-0.58f, 0.814f) 
};

#define SAMPLE_STEPS 2

static const float AORadiusInShader = 0.125f;
static const float AmbientOcclusionPower = 2.0f;
static const float AmbientOcclusionIntensity = 0.5f;
static const float AmbientOcclusionBias = 0.003f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSAO : register(b0)
{
	float4x4 WorldToView;
	int Width;
	int Height;
	float2 RandomationSize;
	float2 TemporalOffset;
	float Near;
	float Far;
	float InvTanHalfFov;
}

Texture2D DepthMap : register(t0);
Texture2D NormalMap : register(t1);
SamplerState PointClampSmp : register(s0);

Texture2D RandomNormalTex : register(t2);
SamplerState PointWrapSmp : register(s1);

static const uint3 k = uint3(0x456789abu, 0x6789ab45u, 0x89ab4567u);
static const uint3 u = uint3(1, 2, 3);
static const uint UINT_MAX = 0xffffffffu;

uint2 uhash22(uint2 n)
{
	n ^= (n.yx << u.xy);
	n ^= (n.yx >> u.xy);
	n *= k.xy;
	n ^= (n.yx << u.xy);
	return n * k.xy;
}

float2 hash22(float2 p)
{
	uint2 n = asuint(p);
	return float2(uhash22(n)) / UINT_MAX;
}

float ConvertFromDeviceZtoLinearZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * linearZ) / (Far - Near) - Far * Near / (Far - Near)) / linearZ
	return (Far * Near) / (Far - deviceZ * (Far - Near));
}

float3 ReconstructCSPos(float sceneDepth, float2 screenPos)
{
	return float3(screenPos * sceneDepth, sceneDepth);
}

float3 WedgeWithNormal(float2 screenSpacePosCenter, float2 localRandom, float3 invFovFix, float3 viewSpacePosition, float3 scaledViewSpaceNormal, float invHaloSize)
{
	float2 screenSpacePosL = screenSpacePosCenter + localRandom;
	float2 screenSpacePosR = screenSpacePosCenter - localRandom;

	float absL = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);
	float absR = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);

	float3 samplePositionL = ReconstructCSPos(absL, screenSpacePosL);
	float3 samplePositionR = ReconstructCSPos(absR, screenSpacePosR);

	float3 deltaL = (samplePositionL - viewSpacePosition) * invFovFix;
	float3 deltaR = (samplePositionR - viewSpacePosition) * invFovFix;

#if OPTIMIZATION_O1 
	float invNormalAngleL = saturate(dot(deltaL, scaledViewSpaceNormal) / dot(deltaL, deltaL));
	float invNormalAngleR = saturate(dot(deltaR, scaledViewSpaceNormal) / dot(deltaR, deltaR));
	float weight = 1.0f;
#else
	float invNormalAngleL = saturate(dot(deltaL, scaledViewSpaceNormal) * rsqrt(dot(deltaL, deltaL)));
	float invNormalAngleR = saturate(dot(deltaR, scaledViewSpaceNormal) * rsqrt(dot(deltaR, deltaR)));
	float weight = saturate(1.0f - length(deltaL) * invHaloSize) * saturate(1.0f - length(deltaR) * invHaloSize);
#endif

	return float3(invNormalAngleL, invNormalAngleR, weight);
}

float2 WedgeWithNormalMonguri(float2 screenSpacePosCenter, float2 localRandom, float3 invFovFix, float3 viewSpacePosition, float3 scaledViewSpaceNormal, float invHaloSize)
{
	float2 screenSpacePosL = screenSpacePosCenter + localRandom;
	float2 screenSpacePosR = screenSpacePosCenter - localRandom;

	float absL = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);
	float absR = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);

	float3 samplePositionL = ReconstructCSPos(absL, screenSpacePosL);
	float3 samplePositionR = ReconstructCSPos(absR, screenSpacePosR);

	// TODO: if deltaL and deltaR is same direction?
	float3 deltaL = (samplePositionL - viewSpacePosition) * invFovFix;
	float3 deltaR = (samplePositionR - viewSpacePosition) * invFovFix;

	float normAngle = acos(dot(deltaL, deltaR) * rsqrt(dot(deltaL, deltaL) * dot(deltaR, deltaR))) / F_PI;

	float dotLtoN = dot(deltaL, scaledViewSpaceNormal) / length(deltaL);
	if (dotLtoN < 0.0f)
	{
		float clampAngle = acos(dotLtoN) / F_PI - 0.5f;
		normAngle -= clampAngle;
	}

	float dotRtoN = dot(deltaR, scaledViewSpaceNormal) / length(deltaR);
	if (dotRtoN < 0.0f)
	{
		float clampAngle = acos(dotRtoN) / F_PI - 0.5f;
		normAngle -= clampAngle;
	}

	// TODO
	float weight = 1.0f;

	return float2(1 - normAngle, weight);
}

float2 WedgeNoNormal(float2 screenSpacePosCenter, float2 localRandom, float3 invFovFix, float3 viewSpacePosition)
{
	float2 screenSpacePosL = screenSpacePosCenter + localRandom;
	float2 screenSpacePosR = screenSpacePosCenter - localRandom;

	float absL = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosL * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);
	float absR = ConvertFromDeviceZtoLinearZ(DepthMap.Sample(PointClampSmp, screenSpacePosR * float2(0.5f, -0.5f) + float2(0.5f, 0.5f)).r);

	float3 samplePositionL = ReconstructCSPos(absL, screenSpacePosL);
	float3 samplePositionR = ReconstructCSPos(absR, screenSpacePosR);

	float3 deltaL = (samplePositionL - viewSpacePosition) * invFovFix;
	float3 deltaR = (samplePositionR - viewSpacePosition) * invFovFix;

	float flatSurfaceBias = 0.05f;

	float left = viewSpacePosition.z - absL;
	float right = viewSpacePosition.z - absR;

	float normAngle = acos(dot(deltaL, deltaR) * rsqrt(dot(deltaL, deltaL) * dot(deltaR, deltaR))) / F_PI;

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
	float2 viewportUVtoRandomUV = float2(Width, Height) / RandomationSize;

	float3 fovFix = float3(InvTanHalfFov, InvTanHalfFov * Width / Height, 1.0f);
	float3 invFovFix = 1.0f / fovFix;

	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;
	float sceneDepth = ConvertFromDeviceZtoLinearZ(deviceZ);

	float3 worldNormal = NormalMap.Sample(PointClampSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	float3 viewSpaceNormal = normalize(mul((float3x3)WorldToView, worldNormal));

	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	// [-depth,depth]x[-depth,depth]x[near,far] i.e. view space pos.
	float3 viewSpacePosition = ReconstructCSPos(sceneDepth, screenPos);

	float actualAORadius = AORadiusInShader * sceneDepth;

	if (USE_NORMALS)
	{
		viewSpacePosition += AmbientOcclusionBias * sceneDepth * viewSpaceNormal * fovFix;
	}

#if 0
	float2 randomVec = normalize(max(hash22(input.TexCoord), 0.000001f)) * actualAORadius;
#else
	float2 randomVec = (RandomNormalTex.Sample(PointWrapSmp, input.TexCoord * viewportUVtoRandomUV + TemporalOffset).rg * 2.0f - 1.0f) * actualAORadius;
#endif

	float2 fovFixXY = fovFix.xy * (1.0f / viewSpacePosition.z);
	float4 randomBase = float4(randomVec, -randomVec.y, randomVec.x) * float4(fovFixXY, fovFixXY);

	// [-1,1]x[-1,1]
	float2 screenSpacePos = viewSpacePosition.xy / viewSpacePosition.z;

	float invHaloSize = 1.0f / (actualAORadius * fovFixXY.x * 2);

	float3 scaledViewSpaceNormal = viewSpaceNormal;
#if OPTIMIZATION_O1 
	scaledViewSpaceNormal *= 8.0f * sceneDepth;
#endif

	float2 weightAccumulator = 0.0001f;

	// disk random loop
	for (int i = 0; i < SAMPLESET_ARRAY_SIZE; i++)
	{
		float2 unrotatedRandom = OcclusionSamplesOffsets[i];
		float2 localRandom = (unrotatedRandom.x * randomBase.xy + unrotatedRandom.y * randomBase.zw);

		if (USE_NORMALS)
		{
			float3 localAllumulator = 0.0f;

			// ray-march loop
			for (uint step = 0; step < SAMPLE_STEPS; step++)
			{
				float scale = (step + 1) / (float)SAMPLE_STEPS;

				float3 stepSample = WedgeWithNormal(screenSpacePos, scale * localRandom, invFovFix, viewSpacePosition, scaledViewSpaceNormal, invHaloSize);
				localAllumulator = lerp(localAllumulator, float3(max(localAllumulator.xy, stepSample.xy), 1), stepSample.z);
			}

			weightAccumulator += float2((1 - localAllumulator.x) * (1 - localAllumulator.x) * localAllumulator.z, localAllumulator.z);
			weightAccumulator += float2((1 - localAllumulator.y) * (1 - localAllumulator.y) * localAllumulator.z, localAllumulator.z);
		}
		else if (USE_NORMALS_MONGURI)
		{
			float2 localAllumulator = 0.0f;

			// ray-march loop
			for (uint step = 0; step < SAMPLE_STEPS; step++)
			{
				float scale = (step + 1) / (float)SAMPLE_STEPS;

				float2 stepSample = WedgeWithNormalMonguri(screenSpacePos, scale * localRandom, invFovFix, viewSpacePosition, scaledViewSpaceNormal, invHaloSize);
				localAllumulator = lerp(localAllumulator, float2(max(localAllumulator.x, stepSample.x), 1), stepSample.y);
			}

			weightAccumulator += float2((1 - localAllumulator.x) * (1 - localAllumulator.x) * localAllumulator.y, localAllumulator.y);
		}
		else
		{
			float2 localAllumulator = 0.0f;

			// ray-march loop
			for (uint step = 0; step < SAMPLE_STEPS; step++)
			{
				float scale = (step + 1) / (float)SAMPLE_STEPS;

				float2 stepSample = WedgeNoNormal(screenSpacePos, scale * localRandom, invFovFix, viewSpacePosition);
				localAllumulator = lerp(localAllumulator, float2(max(localAllumulator.x, stepSample.x), 1), stepSample.y);
			}

			weightAccumulator += float2((1 - localAllumulator.x) * (1 - localAllumulator.x) * localAllumulator.y, localAllumulator.y);
		}
	}

	float result = weightAccumulator.x / weightAccumulator.y;
	// abs() to prevent shader warning
	result = 1 - (1 - pow(abs(result), AmbientOcclusionPower)) * AmbientOcclusionIntensity;

	return float4(result, 0, 0, 1);
}