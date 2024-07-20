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
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_BORDER"\
", addressV = TEXTURE_ADDRESS_BORDER"\
", addressW = TEXTURE_ADDRESS_BORDER"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

// must be the same value with cpp
#define GAUSSIAN_FILTER_SAMPLES 32

cbuffer CbFilter : register(b0)
{
	float4 SampleOffsets[GAUSSIAN_FILTER_SAMPLES / 2] : packoffset(c0);
	float4 SampleWeights[GAUSSIAN_FILTER_SAMPLES] : packoffset(c16);
	int NumSample : packoffset(c48);
	int bEnableAdditveTexture : packoffset(c48.y);
}

Texture2D SrcColorMap : register(t0);
Texture2D AdditiveColorMap : register(t1);
SamplerState LinearMipPointBorderSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
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