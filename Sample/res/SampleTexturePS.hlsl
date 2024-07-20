#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

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

[RootSignature(ROOT_SIGNATURE)]
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