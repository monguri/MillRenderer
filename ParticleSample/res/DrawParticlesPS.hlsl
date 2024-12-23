//#define DYNAMIC_RESOURCES

#ifdef DYNAMIC_RESOURCES
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | SAMPLER_HEAP_DIRECTLY_INDEXED"\
" | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"\
")"\

#else
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_VERTEX)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_VERTEX)"\

#endif

[RootSignature(ROOT_SIGNATURE)]
float4 main() : SV_TARGET
{
	return float4(1.0f, 0.0f, 0.0f, 1.0f);
}