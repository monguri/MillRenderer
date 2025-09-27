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
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
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

Texture2D ColorMap : register(t0);
Texture2D SSAOMap : register(t1);
Texture2D SSGIMap : register(t2);
SamplerState PointClampSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float4 color = ColorMap.Sample(PointClampSmp, input.TexCoord);
	float ssao = SSAOMap.Sample(PointClampSmp, input.TexCoord).r;
	float3 ssgi = SSGIMap.Sample(PointClampSmp, input.TexCoord).rgb;
	return float4(color.rgb * ssao + ssgi, color.a);
}