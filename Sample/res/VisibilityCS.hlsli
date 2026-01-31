// TODO: Ç∆ÇËÇ†Ç¶Ç∏MeshletÅADynamicResourceÇÃÇ∆Ç´Ç…é¿ëïÇå¿íËÇ∑ÇÈ
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"DENY_VERTEX_SHADER_ROOT_ACCESS"\
" | DENY_PIXEL_SHADER_ROOT_ACCESS"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
" | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"\
")"\
", RootConstants(num32BitConstants=8, b0)"\
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

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
};

struct VertexData
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD;
};

struct PrimitiveData
{
	uint MeshIdx : MESH_INDEX;
	uint MeshletIdx : MESHLET_INDEX;
	uint TriangleIdx : TRIANGLE_INDEX;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

struct DescHeapIndices
{
	uint CbTransform;
	uint CbMesh;
	uint SbVertexBuffer;
	uint SbMeshlets;
	uint SbMeshletVertices;
	uint SbMeshletTriangles;
	uint CbMaterial;
	uint BaseColorMap;
};

struct Transform
{
	float4x4 ViewProj;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
	uint MaterialID;
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b0);

groupshared VertexData outVerts[64];

void softwareRasterize(float4 csPos0, float4 csPos1, float4 csPos2, PrimitiveData primData)
{
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID
)
{
	ConstantBuffer<Transform> CbTransform = ResourceDescriptorHeap[CbDescHeapIndices.CbTransform];
	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[CbDescHeapIndices.CbMesh];

	StructuredBuffer<VSInput> vertexBuffer = ResourceDescriptorHeap[CbDescHeapIndices.SbVertexBuffer];
	StructuredBuffer<meshopt_Meshlet> meshlets = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshlets];
	StructuredBuffer<uint> meshletsVertices = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletVertices];
	StructuredBuffer<uint> meshletsTriangles = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletTriangles];
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
#ifdef ALPHA_MODE_MASK
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
#endif

	meshopt_Meshlet meshlet = meshlets[gid];

	if (gtid < meshlet.VertCount)
	{
		uint vertexIndex = meshletsVertices[meshlet.VertOffset + gtid];
		VSInput input = vertexBuffer[vertexIndex];

		float4 localPos = float4(input.Position, 1.0f);
		float4 worldPos = mul(CbMesh.World, localPos);
		float4 projPos = mul(CbTransform.ViewProj, worldPos);

		VertexData v;
		v.Position = projPos;
		v.TexCoord = input.TexCoord;
		outVerts[gtid] = v;
	}

	GroupMemoryBarrierWithGroupSync();

	if (gtid < meshlet.TriCount)
	{
		uint triBaseIdx = meshlet.TriOffset + gtid * 3;

		float4 csPos0 = outVerts[meshletsTriangles[triBaseIdx + 0]].Position;
		float4 csPos1 = outVerts[meshletsTriangles[triBaseIdx + 1]].Position;
		float4 csPos2 = outVerts[meshletsTriangles[triBaseIdx + 2]].Position;

		PrimitiveData primData;
		primData.MeshIdx = CbMesh.MeshIdx;
		primData.MeshletIdx = gid;
		primData.TriangleIdx = gtid;

		softwareRasterize(csPos0, csPos1, csPos2, primData);
	}
}