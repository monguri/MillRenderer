struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

// must be the same value with cpp
#define GAUSSIAN_FILTER_SAMPLES 32

cbuffer CbFilter : register(b0)
{
	float2 SampleOffsets[GAUSSIAN_FILTER_SAMPLES];
	float SampleWeights[GAUSSIAN_FILTER_SAMPLES];
	int NumSample;
	int bEnableAdditveTexture;
}

Texture2D SrcColorMap : register(t0);
SamplerState LinearClampMipPointSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 uv = input.TexCoord;


	float3 accumColor = float3(0, 0, 0);
	float accumWeight = 0;
	for (int i = 0; i < NumSample; i++)
	{
		float weight = SampleWeights[i];
		accumColor += SrcColorMap.Sample(LinearClampMipPointSmp, uv + SampleOffsets[i]).rgb * weight;
		accumWeight += weight;
	}

	float3 filteredColor = accumColor / accumWeight;

	return float4(filteredColor, 1);
}