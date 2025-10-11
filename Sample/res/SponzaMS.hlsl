#include "ShadowMap.hlsli"

#ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
	#define ROOT_SIGNATURE ""\
	"RootFlags"\
	"("\
	"DENY_VERTEX_SHADER_ROOT_ACCESS"\
	" | DENY_HULL_SHADER_ROOT_ACCESS"\
	" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
	" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
	" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
	")"\
	", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_MESH)"\
	", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
	", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
	", StaticSampler"\
	"("\
	"s0"\
	", filter = FILTER_ANISOTROPIC"\
	", addressU = TEXTURE_ADDRESS_WRAP"\
	", addressV = TEXTURE_ADDRESS_WRAP"\
	", addressW = TEXTURE_ADDRESS_WRAP"\
	", maxAnisotropy = 16"\
	", comparisonFunc = COMPARISON_NEVER"\
	", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
	", visibility = SHADER_VISIBILITY_PIXEL"\
	")"\
	", StaticSampler"\
	"("\
	"s1"\
	", filter = FILTER_MIN_MAG_MIP_POINT"\
	", addressU = TEXTURE_ADDRESS_CLAMP"\
	", addressV = TEXTURE_ADDRESS_CLAMP"\
	", addressW = TEXTURE_ADDRESS_CLAMP"\
	", maxAnisotropy = 1"\
	", comparisonFunc = COMPARISON_NEVER"\
	", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
	", visibility = SHADER_VISIBILITY_PIXEL"\
	")"
#else // #ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
	#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#define ROOT_SIGNATURE ""\
		"RootFlags"\
		"("\
		"DENY_VERTEX_SHADER_ROOT_ACCESS"\
		" | DENY_HULL_SHADER_ROOT_ACCESS"\
		" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
		" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
		" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
		")"\
		", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", StaticSampler"\
		"("\
		"s0"\
		", filter = FILTER_ANISOTROPIC"\
		", addressU = TEXTURE_ADDRESS_WRAP"\
		", addressV = TEXTURE_ADDRESS_WRAP"\
		", addressW = TEXTURE_ADDRESS_WRAP"\
		", maxAnisotropy = 16"\
		", comparisonFunc = COMPARISON_NEVER"\
		", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"\
		", StaticSampler"\
		"("\
		"s1"\
		", filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT"\
		", addressU = TEXTURE_ADDRESS_CLAMP"\
		", addressV = TEXTURE_ADDRESS_CLAMP"\
		", addressW = TEXTURE_ADDRESS_CLAMP"\
		", maxAnisotropy = 1"\
		", comparisonFunc = COMPARISON_LESS_EQUAL"\
		", borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"
	#else // #ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#define ROOT_SIGNATURE ""\
		"RootFlags"\
		"("\
		"DENY_VERTEX_SHADER_ROOT_ACCESS"\
		" | DENY_HULL_SHADER_ROOT_ACCESS"\
		" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
		" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
		" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
		")"\
		", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_MESH)"\
		", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", StaticSampler"\
		"("\
		"s0"\
		", filter = FILTER_ANISOTROPIC"\
		", addressU = TEXTURE_ADDRESS_WRAP"\
		", addressV = TEXTURE_ADDRESS_WRAP"\
		", addressW = TEXTURE_ADDRESS_WRAP"\
		", maxAnisotropy = 16"\
		", comparisonFunc = COMPARISON_NEVER"\
		", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"\
		", StaticSampler"\
		"("\
		"s1"\
		", filter = FILTER_MIN_MAG_LINEAR_MIP_POINT"\
		", addressU = TEXTURE_ADDRESS_CLAMP"\
		", addressV = TEXTURE_ADDRESS_CLAMP"\
		", addressW = TEXTURE_ADDRESS_CLAMP"\
		", maxAnisotropy = 1"\
		", comparisonFunc = COMPARISON_NEVER"\
		", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"
	#endif // #ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
#endif // #ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP

//TODO: SponzaVS.hlsl��SponzaMS.hlsl�ō\���̒�`���d�����Ă���
//TODO: BasePassVS.hlsl��BasePassMS.hlsl�ō\���̒�`���d�����Ă���
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
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
	float3 DirLightShadowCoord : TEXCOORD2;
	float3 SpotLight1ShadowCoord : TEXCOORD3;
	float3 SpotLight2ShadowCoord : TEXCOORD4;
	float3 SpotLight3ShadowCoord : TEXCOORD5;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

cbuffer CbTransform : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
	float4x4 WorldToDirLightShadowMap : packoffset(c4);
	float4x4 WorldToSpotLight1ShadowMap : packoffset(c8);
	float4x4 WorldToSpotLight2ShadowMap : packoffset(c12);
	float4x4 WorldToSpotLight3ShadowMap : packoffset(c16);
}

cbuffer CbMesh : register(b1)
{
	float4x4 World;
}

StructuredBuffer<VSInput> vertexBuffer : register(t0);
StructuredBuffer<meshopt_Meshlet> meshlets : register(t1);
StructuredBuffer<uint> meshletsVertices : register(t2);
StructuredBuffer<uint> meshletsTriangles : register(t3);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VSOutput outVerts[64],
	out indices uint3 outTriIndices[126]
)
{
	meshopt_Meshlet meshlet = meshlets[gid];

	SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);

	if (gtid < meshlet.VertCount)
	{
		uint vertexIndex = meshletsVertices[meshlet.VertOffset + gtid];
		VSInput input = vertexBuffer[vertexIndex];

		float4 localPos = float4(input.Position, 1.0f);
		float4 worldPos = mul(World, localPos);
		float4 projPos = mul(ViewProj, worldPos);

		VSOutput output = (VSOutput)0;
		output.Position = projPos;
		output.TexCoord = input.TexCoord;
		output.WorldPos = worldPos.xyz;

		float4 dirLightShadowPos = mul(WorldToDirLightShadowMap, worldPos);
		// dividing by w is not necessary because it is 1 by orthogonal.
		output.DirLightShadowCoord = dirLightShadowPos.xyz / dirLightShadowPos.w;

		float4 spotLight1ShadowPos = mul(WorldToSpotLight1ShadowMap, worldPos);
		output.SpotLight1ShadowCoord = spotLight1ShadowPos.xyz / spotLight1ShadowPos.w;

		float4 spotLight2ShadowPos = mul(WorldToSpotLight2ShadowMap, worldPos);
		output.SpotLight2ShadowCoord = spotLight2ShadowPos.xyz / spotLight2ShadowPos.w;

		float4 spotLight3ShadowPos = mul(WorldToSpotLight3ShadowMap, worldPos);
		output.SpotLight3ShadowCoord = spotLight3ShadowPos.xyz / spotLight3ShadowPos.w;

		float3 N = normalize(mul((float3x3)World, input.Normal));
		float3 T = normalize(mul((float3x3)World, input.Tangent));
		float3 B = normalize(cross(N, T));

		output.InvTangentBasis = transpose(float3x3(T, B, N));
		// TODO:ShadowCoord���ǉ�
		outVerts[gtid] = output;
	}

	if (gtid < meshlet.TriCount)
	{
#if 0
		outTriIndices[gtid] = meshletsTriangles.Load3(meshlet.TriOffset + gtid * 3);
#else
		outTriIndices[gtid] = uint3(
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 0],
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 1],
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 2]
		);
#endif
	}
}