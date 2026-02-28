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
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_VERTEX)"\

struct Transform
{
	float4x4 ViewProj;
	float4x4 WorldToDirLightShadowMap;
	float4x4 WorldToSpotLight1ShadowMap;
	float4x4 WorldToSpotLight2ShadowMap;
	float4x4 WorldToSpotLight3ShadowMap;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

ConstantBuffer<Transform> CbTransform : register(b0);
ConstantBuffer<Mesh> CbMesh : register(b1);

[RootSignature(ROOT_SIGNATURE)]
float4 main( float3 pos : POSITION ) : SV_POSITION
{
	return mul(CbTransform.ViewProj, mul(CbMesh.World, float4(pos, 1)));
}