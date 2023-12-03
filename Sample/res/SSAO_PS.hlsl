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
	float InvTanHalfFov;
}

Texture2D DepthMap : register(t0);
SamplerState DepthSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 fovFix = float3(InvTanHalfFov, InvTanHalfFov * Width / Height, 1.0f);
	float3 invFovFix = 1.0f / fovFix;

	float depth = DepthMap.Sample(DepthSmp, input.TexCoord).r;
	float ActualAORadius = AORadiusInShader * depth;
	float2 randomVec = float2(0, 1) * ActualAORadius;

	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	// TODO:why screenPos * (1.0f - depth) is necessary?
	// [-1,1]x[-1,1]x[0,1]
	float3 viewSpacePos = float3(screenPos * (1.0f - depth), depth);

	float2 fovFixXY = fovFix.xy * (1.0f / min(1.0f - depth, 1e-6f));
	float4 randomBase = float4(randomVec, -randomVec.y, randomVec.x) * float4(fovFixXY, fovFixXY);

	float accum = 0;

	for (int i = 0; i < SAMPLESET_ARRAY_SIZE; i++)
	{
		float2 unrotatedRandom = OcclusionSamplesOffsets[i];

		for (uint step = 0; step < SAMPLE_STEP; step++)
		{
			float scale = (step + 1) / SAMPLE_STEP;

		}
	}

	return depth;
}