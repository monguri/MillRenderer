struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbBloomSetup : register(b0)
{
	float BloomThreshold;
	int bEnableSSAO;
}

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}