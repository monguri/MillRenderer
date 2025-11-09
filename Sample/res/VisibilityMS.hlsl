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
	float4 position : SV_Position;
};

struct PrimitiveData
{
	uint primitiveID : SV_PrimitiveID;
	uint meshletID : MESHLET_ID;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

// TODO: とりあえずMeshlet、DynamicResourceのときに実装を限定する
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
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b0);

StructuredBuffer<VSInput> vertexBuffer : register(t0);
StructuredBuffer<meshopt_Meshlet> meshlets : register(t1);
StructuredBuffer<uint> meshletsVertices : register(t2);
StructuredBuffer<uint> meshletsTriangles : register(t3);

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
		v.position = projPos;
		outVerts[gtid] = v;
	}

	if (gtid < 126)
	{
		outTriIndices[gtid] = uint3(
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 0],
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 1],
			meshletsTriangles[meshlet.TriOffset + gtid * 3 + 2]
		);
	}

	if (gtid < 126)
	{
		PrimitiveData p;
		p.primitiveID = gtid;
		p.meshletID = gid;
		outPrims[gtid] = p;
	}
}