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

Texture2D DepthMap : register(t0);
SamplerState DepthSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float depth = DepthMap.Sample(DepthSmp, input.TexCoord).r;
	float ActualAORadius = AORadiusInShader * depth;
	float2 randomVec = float2(0, 1) * ActualAORadius;

	for (int i = 0; i < SAMPLESET_ARRAY_SIZE; i++)
	{
		float2 unrotatedRandom = OcclusionSamplesOffsets[i];

		for (uint step = 0; step < SAMPLE_STEP; step++)
		{
			float scale = (step + 1) / SAMPLESET_ARRAY_SIZE;
		}
	}

	return depth;
}