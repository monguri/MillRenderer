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
	float4x4 WorldToView;
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

float4 main(const VSOutput input) : SV_TARGET0
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}