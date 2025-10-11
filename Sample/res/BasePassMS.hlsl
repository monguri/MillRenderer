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

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

//TODO: SponzaVS.hlslとSponzaMS.hlslで構造体定義が重複している
//TODO: BasePassVS.hlslとBasePassMS.hlslで構造体定義が重複している
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

StructuredBuffer<VSInput> vertexBuffer : register(t0);
StructuredBuffer<meshopt_Meshlet> meshlets : register(t1);
StructuredBuffer<uint> meshletsVertices : register(t2);
#if 0
ByteAddressBuffer meshletsTriangles : register(t3);
#else
StructuredBuffer<uint> meshletsTriangles : register(t3);
#endif

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

		float3 N = normalize(mul((float3x3)World, input.Normal));
		float3 T = normalize(mul((float3x3)World, input.Tangent));
		float3 B = normalize(cross(N, T));

		output.InvTangentBasis = transpose(float3x3(T, B, N));
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