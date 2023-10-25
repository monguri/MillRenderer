struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D SphereMap : register(t0);
SamplerState SphereSmp : register(s0);

float4 main(VSOutput input) : SV_TARGET0
{
	return SphereMap.SampleLevel(SphereSmp, input.TexCoord, 0.0f);
}