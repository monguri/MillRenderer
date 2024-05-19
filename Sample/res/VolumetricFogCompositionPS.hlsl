struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	return float4(Color, 1.0f);
}