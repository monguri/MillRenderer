struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbAmbientLight : register(b0)
{
	float Intensity;
}

Texture2D ColorMap : register(t0);
SamplerState ColorSmp : register(s0);

Texture2D SSAOMap : register(t1);
SamplerState SSAOSmp : register(s1);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = ColorMap.Sample(ColorSmp, input.TexCoord);
	float ssao = SSAOMap.Sample(SSAOSmp, input.TexCoord).r;
	float3 result = color.rgb + ssao.rrr * Intensity;
	return float4(result, color.a);
}