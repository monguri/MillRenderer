struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D SrcColorMap : register(t0);
SamplerState LinearClampMipPointSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 result = SrcColorMap.Sample(LinearClampMipPointSmp, input.TexCoord);
	return result;
}