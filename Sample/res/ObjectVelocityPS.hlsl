#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_VERTEX)"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 CurClipPos : CUR_CLIP_POSITION;
	float4 PrevClipPos : PREV_CLIP_POSITION;
};

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float4 curNDCPos = input.CurClipPos / input.CurClipPos.w;
	float2 curUV = (curNDCPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	float4 prevNDCPos = input.PrevClipPos / input.PrevClipPos.w;
	float2 prevUV = (prevNDCPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	return float4(curUV - prevUV, 0, 1);
}