#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\

struct CbCamera
{
	float4x4 View;
	float4x4 Proj;
};

struct CbMesh
{
	float4x4 World;
};

struct Meshlet
{
	uint vertCount;
	uint vertOffset;
	uint triCount;
	uint triOffset;
};

struct Vertex
{
	float3 position;
	float3 normal;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	uint meshletIndex: MESHLET_INDEX;
};

ConstantBuffer<CbCamera> cbCamera : register(b0);
ConstantBuffer<CbMesh> cbMesh : register(b1);
StructuredBuffer<Meshlet> meshlets : register(t0);
StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint> indices : register(t2);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VertexOutput outVerts[64],
	out indices uint3 outTriIndices[126]
)
{
	Meshlet meshlet = meshlets[gid];
	SetMeshOutputCounts(meshlet.vertCount, meshlet.triCount);

	if (gtid < 64)
	{
		outVerts[gtid].position = 0;
		outVerts[gtid].normal = 1;
		outVerts[gtid].meshletIndex = gtid;
	}

	if (gtid < 127)
	{
		outTriIndices[gtid] = 0;
	}
}