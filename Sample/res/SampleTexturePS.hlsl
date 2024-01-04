struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D Texture : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
#if 1
	return Texture.Sample(PointClampSmp, input.TexCoord);
#else
	// TODO: for SSAO
	float r = Texture.Sample(PointClampSmp, input.TexCoord).r;
	return float4(r, r, r, 1);
#endif
}