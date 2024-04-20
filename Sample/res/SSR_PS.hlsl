struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSR : register(b0)
{
	int Width;
	int Height;
	int FrameSampleIndex;
	int bEnableSSR;
}

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture2D NormalMap : register(t2);
// TODO: should be PointClamp?
SamplerState PointClampSmp : register(s0);

// Referenced UE's InterleavedGradientNoise
float InterleavedGradientNoise(float2 UV, float FrameId)
{
	// magic values are found by experimentation
	UV += FrameId * (float2(47, 17) * 0.695f);

	const float3 MAGIC_NUMBER = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(MAGIC_NUMBER.z * frac(dot(UV, MAGIC_NUMBER.xy)));
}

float4 main(const VSOutput input) : SV_TARGET0
{
	if (bEnableSSR)
	{
		float3 Color = InterleavedGradientNoise(input.TexCoord * float2(Width, Height), FrameSampleIndex);
		return float4(Color, 1.0f);
	}
	else
	{
		float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
		return float4(Color, 1.0f);
	}
}