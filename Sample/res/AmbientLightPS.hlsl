struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbAmbientLight : register(b0)
{
	float Intensity;
}

Texture2D SceneColorMap : register(t0);
SamplerState SceneColorSmp : register(s0);

Texture2D SSAOMap : register(t1);
SamplerState SSAOSmp : register(s1);

float4 main(const VSOutput input) : SV_TARGET0
{
	return float4(1, 0, 0, 1);
}