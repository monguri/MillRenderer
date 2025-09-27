#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
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

cbuffer CbDownsample : register(b0)
{
	int SrcWidth : packoffset(c0);
	int SrcHeight : packoffset(c0.y);
}

Texture2D SrcColorMap : register(t0);
SamplerState LinearClampMipPointSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float2 InvExtent = float2(1.0f / SrcWidth, 1.0f / SrcHeight);

	float2 UVs[4];
	UVs[0] = input.TexCoord + float2(-1, -1) * InvExtent;
	UVs[1] = input.TexCoord + float2(1, -1) * InvExtent;
	UVs[2] = input.TexCoord + float2(-1, 1) * InvExtent;
	UVs[3] = input.TexCoord + float2(1, 1) * InvExtent;

	float4 samples[4];
	for (uint i = 0; i < 4; i++)
	{
		samples[i] = SrcColorMap.Sample(LinearClampMipPointSmp, UVs[i]);
	}

	return (samples[0] + samples[1] + samples[2] + samples[3]) * 0.25f;
}