struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D ColorMap : register(t0);
SamplerState ColorSmp : register(s0);

Texture2D SSAOMap : register(t1);
SamplerState SSAOSmp : register(s1);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = ColorMap.Sample(ColorSmp, input.TexCoord);
	float ssao = SSAOMap.Sample(SSAOSmp, input.TexCoord).r;
	return float4(color.rgb * ssao, color.a);
}