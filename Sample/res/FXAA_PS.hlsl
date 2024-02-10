struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbFXAA : register(b0)
{
	int Width;
	int Height;
	int bEnableFXAA;
}

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	return ColorMap.Sample(PointClampSmp, input.TexCoord);
}
