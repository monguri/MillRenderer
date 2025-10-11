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

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 CurClipPos : CUR_CLIP_POSITION;
	float4 PrevClipPos : PREV_CLIP_POSITION;
};

cbuffer CbObjectVelocity : register(b0)
{
	float4x4 CurWVPWithJitter : packoffset(c0);
	float4x4 CurWVPNoJitter : packoffset(c4);
	float4x4 PrevWVPNoJitter : packoffset(c8);
}

[RootSignature(ROOT_SIGNATURE)]
VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float4 localPos = float4(input.Position, 1.0f);
	output.Position = mul(CurWVPWithJitter, localPos);
	output.CurClipPos = mul(CurWVPNoJitter, localPos);
	output.PrevClipPos = mul(PrevWVPNoJitter, localPos);

	return output;
}