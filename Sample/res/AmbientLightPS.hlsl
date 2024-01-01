struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D ColorMap : register(t0);
Texture2D SSAOMap : register(t1);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = ColorMap.Sample(PointClampSmp, input.TexCoord);
	float ssao = SSAOMap.Sample(PointClampSmp, input.TexCoord).r;
	return float4(color.rgb * ssao, color.a);
}