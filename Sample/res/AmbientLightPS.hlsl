struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D ColorMap : register(t0);
Texture2D SSAOMap : register(t1);
Texture2D SSGIMap : register(t2);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = ColorMap.Sample(PointClampSmp, input.TexCoord);
	float ssao = SSAOMap.Sample(PointClampSmp, input.TexCoord).r;
	float3 ssgi = SSGIMap.Sample(PointClampSmp, input.TexCoord).rgb;
	return float4(color.rgb * ssao + ssgi, color.a);
}