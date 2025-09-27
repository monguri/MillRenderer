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
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_VERTEX)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

TextureCube CubeMap : register(t0);
SamplerState CubeSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
PSOutput main(const VSOutput input)
{
	PSOutput output;

	output.Color = CubeMap.SampleLevel(CubeSmp, input.TexCoord, 0);

	// 法線は(0, 0, 0)扱い
	output.Normal.xyz = 0.5f;
	output.Normal.a = 1;

	// 反射を起こさないようにメタリックは0、roughnessは1
	output.MetallicRoughness.r = 0;
	output.MetallicRoughness.g = 1;

	return output;
}