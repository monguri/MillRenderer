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

float ConvertFromDeviceZtoLinearZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * linearZ) / (Far - Near) - Far * Near / (Far - Near)) / linearZ
	return (Far * Near) / (Far - deviceZ * (Far - Near));
}

float3 ConverFromNDCToWS(float4 ndcPos)
{
	float deviceZ = ndcPos.z;
	float linearDepth = ConvertFromDeviceZtoLinearZ(deviceZ);

	// linearDepth is clip space w. so multiply w to ndc position to get clip space position.
	float4 clipPos = ndcPos * -linearDepth;
	float4 worldPos = mul(InvViewProjMatrix, clipPos);
	
	return worldPos.xyz;
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 worldPos = ConverFromNDCToWS(ndcPos);

	float3 worldNormal = NormalMap.Sample(PointClampSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	float3 viewSpaceNormal = normalize(mul((float3x3)ViewMatrix, worldNormal));

	//// [-depth,depth]x[-depth,depth]x[near,far] i.e. view space pos.
	//float3 viewSpacePosition = ConverFromSSPosToVSPos(screenPos, sceneDepth);

	float result = 1.0f;

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