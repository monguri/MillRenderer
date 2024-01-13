struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

// must be the same value with cpp
#define GAUSSIAN_FILTER_SAMPLES 32

cbuffer CbFilter : register(b0)
{
	float4 SampleOffsets[GAUSSIAN_FILTER_SAMPLES / 2];
	float4 SampleWeights[GAUSSIAN_FILTER_SAMPLES];
	int NumSample;
	int bEnableAdditveTexture;
}

Texture2D SrcColorMap : register(t0);
Texture2D AdditiveColorMap : register(t1);
SamplerState LinearMipPointBorderSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 uv = input.TexCoord;
	float4 accumColor = float4(0, 0, 0, 0);

	int sampleIdx = 0;
	for (; sampleIdx < NumSample - 1; sampleIdx += 2)
	{
		float4 offsetedUVUV = uv.xyxy + SampleOffsets[(uint)sampleIdx / 2];
		accumColor += SrcColorMap.Sample(LinearMipPointBorderSmp, offsetedUVUV.xy) * SampleWeights[sampleIdx + 0];
		accumColor += SrcColorMap.Sample(LinearMipPointBorderSmp, offsetedUVUV.zw) * SampleWeights[sampleIdx + 1];
	}

	// The case that NumSample is odd.
	if (sampleIdx < NumSample)
	{
		float2 offsetedUV = uv + SampleOffsets[(uint)sampleIdx / 2].xy;
		accumColor += SrcColorMap.Sample(LinearMipPointBorderSmp, offsetedUV) * SampleWeights[sampleIdx];
	}

	if (bEnableAdditveTexture)
	{
		accumColor += AdditiveColorMap.Sample(LinearMipPointBorderSmp, uv);
	}

	return float4(accumColor.rgb, 1);
}