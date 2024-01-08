struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbDownsample : register(b0)
{
	int SrcWidth;
	int SrcHeight;
}

Texture2D SrcColorMap : register(t0);
SamplerState LinearClampMipPointSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 result = SrcColorMap.Sample(LinearClampMipPointSmp, input.TexCoord);
	return result;
}