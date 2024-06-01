struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSampleTexture : register(b0)
{
	int bOnlyRedChannel : packoffset(c0);
	float Contrast : packoffset(c0.y);
	float Scale : packoffset(c0.z);
	float Bias : packoffset(c0.w);
};

Texture2D Texture : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = Texture.Sample(PointClampSmp, input.TexCoord);
	color.rgb = sign(color.rgb) * pow(abs(color.rgb), Contrast);
	color.rgb *= Scale;
	color.rgb += Bias;
	if (bOnlyRedChannel)
	{
		return float4(color.r, color.r, color.r, color.a);
	}
	else
	{
		return color;
	}
}