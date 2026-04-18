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
", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
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
")"

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
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

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

StructuredBuffer<VSInput> vertexBuffer : register(t0);
ByteAddressBuffer drawMeshletList : register(t1);
StructuredBuffer<meshopt_Meshlet> meshlets : register(t2);
StructuredBuffer<uint> meshletsVertices : register(t3);
StructuredBuffer<uint> meshletsTriangles : register(t4);

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
	uint meshletIdx = drawMeshletList.Load(gid * 4);
	meshopt_Meshlet meshlet = meshlets[meshletIdx];

	SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);

	if (gtid < meshlet.VertCount)
	{
		uint vertexIndex = meshletsVertices[meshlet.VertOffset + gtid];
		VSInput input = vertexBuffer[vertexIndex];

		float4 localPos = float4(input.Position, 1.0f);
		float4 worldPos = mul(CbMesh.World, localPos);
		float4 projPos = mul(CbTransform.ViewProj, worldPos);

		VSOutput output;
		output.Position = projPos;
		output.TexCoord = input.TexCoord;
		outVerts[gtid] = output;
	}

	if (gtid < meshlet.TriCount)
	{
		uint triBaseIndex = meshlet.TriOffset + gtid * 3;
#if 0
		outTriIndices[gtid] = meshletsTriangles.Load3(meshlet.TriOffset + gtid * 3);
#else
		outTriIndices[gtid] = uint3(
			meshletsTriangles[triBaseIndex + 0],
			meshletsTriangles[triBaseIndex + 1],
			meshletsTriangles[triBaseIndex + 2]
		);
#endif
	}
}