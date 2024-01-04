struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D Texture : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	return Texture.Sample(PointClampSmp, input.TexCoord);
}