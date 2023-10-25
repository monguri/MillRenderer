struct VSOutput
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

TextureCube CubeMap : register(t0);
SamplerState CubeSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	return CubeMap.SampleLevel(CubeSmp, input.TexCoord, 0);
}