// referenced UE's BloomThreshod.
// -1:all pixels affect bloom equally (physically correct, faster as a threshold pass is omitted), 0:all pixels affect bloom brights more, 1(default), >1 brighter
static const float LUMA_THRESHOLD = -1.0f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float Luminance(float3 linearColor)
{
	return dot(linearColor, float3(0.3f, 0.59f, 0.11f));
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 linearColor = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	float luminance = Luminance(linearColor);
	float bloomLuma = luminance - LUMA_THRESHOLD;
	float bloomAmount = saturate(bloomLuma * 0.5f);

	return float4(bloomAmount * linearColor, 0);
}