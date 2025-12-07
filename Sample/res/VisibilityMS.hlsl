// TODO: とりあえずMeshlet、DynamicResourceのときに実装を限定する
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"DENY_VERTEX_SHADER_ROOT_ACCESS"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"\
")"\
", RootConstants(num32BitConstants=6, b0, visibility = SHADER_VISIBILITY_MESH)"\
", RootConstants(num32BitConstants=2, b1, visibility = SHADER_VISIBILITY_PIXEL)"\
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

// TODO: VisibiligyBufferの段階ではPosition以外はVBに必要ないので削れる
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
	uint TriangleIdx : SV_PrimitiveID;
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

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b0);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VertexData outVerts[64],
	out indices uint3 outTriIndices[126],
	out primitives PrimitiveData outPrims[126]
)
{
	ConstantBuffer<Transform> CbTransform = ResourceDescriptorHeap[CbDescHeapIndices.CbTransform];
	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[CbDescHeapIndices.CbMesh];

	StructuredBuffer<VSInput> vertexBuffer = ResourceDescriptorHeap[CbDescHeapIndices.SbVertexBuffer];
	StructuredBuffer<meshopt_Meshlet> meshlets = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshlets];
	StructuredBuffer<uint> meshletsVertices = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletVertices];
	StructuredBuffer<uint> meshletsTriangles = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletTriangles];

	meshopt_Meshlet meshlet = meshlets[gid];

	SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);

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

	if (gtid < 126)
	{
		uint triBaseIdx = meshlet.TriOffset + gtid * 3;
		outTriIndices[gtid] = uint3(
			meshletsTriangles[triBaseIdx],
			meshletsTriangles[triBaseIdx + 1],
			meshletsTriangles[triBaseIdx + 2]
		);
	}

	if (gtid < meshlet.TriCount)
	{
		PrimitiveData p;
		p.MeshIdx = CbMesh.MeshIdx;
		p.MeshletIdx = gid;
		p.TriangleIdx = gtid;
		outPrims[gtid] = p;
	}
}