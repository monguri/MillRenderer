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
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
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
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"

cbuffer CbTransform : register(b0)
{
	float4x4 ViewProj;
}

cbuffer CbMesh : register(b1)
{
	float4x4 World;
}

struct MeshletInfo
{
	uint VertCount;
	uint VertOffset;
	uint TriCount;
	uint TriOffset;
};

//TODO: BasePassVS.hlslÇ∆BasePassMS.hlslÇ≈ç\ë¢ëÃíËã`Ç™èdï°ÇµÇƒÇ¢ÇÈ
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
};

StructuredBuffer<MeshletInfo> meshletInfos : register(t0);
StructuredBuffer<VSInput> vertices : register(t1);
StructuredBuffer<uint> indices : register(t2);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VSOutput outVerts[128],
	out indices uint3 outTriIndices[128]
)
{
	MeshletInfo meshletInfo = meshletInfos[gid];

	SetMeshOutputCounts(meshletInfo.VertCount, meshletInfo.TriCount);

	if (gtid < meshletInfo.VertCount)
	{
		VSInput input = vertices[meshletInfo.VertOffset + gtid];

		float4 localPos = float4(input.Position, 1.0f);
		float4 worldPos = mul(World, localPos);
		float4 projPos = mul(ViewProj, worldPos);

		VSOutput output = (VSOutput)0;
		output.Position = projPos;
		output.TexCoord = input.TexCoord;
		output.WorldPos = worldPos.xyz;

		float3 N = normalize(mul((float3x3)World, input.Normal));
		float3 T = normalize(mul((float3x3)World, input.Tangent));
		float3 B = normalize(cross(N, T));

		output.InvTangentBasis = transpose(float3x3(T, B, N));
		outVerts[gtid] = output;
	}

	if (gtid < meshletInfo.TriCount)
	{
		outTriIndices[gtid] = uint3(indices[meshletInfo.TriOffset + gtid], indices[meshletInfo.TriOffset + gtid + 1], indices[meshletInfo.TriOffset + gtid + 2]);
	}
}