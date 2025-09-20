#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\

cbuffer CbTransform : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

cbuffer CbMesh : register(b1)
{
	float4x4 World : packoffset(c0);
}

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

StructuredBuffer<Meshlet> meshlets : register(t0);
StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint> indices : register(t2);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VertexOutput outVerts[128],
	out indices uint3 outTriIndices[128]
)
{
	Meshlet meshlet = meshlets[gid];
	SetMeshOutputCounts(meshlet.vertCount, meshlet.triCount);

	if (gtid < meshlet.vertCount)
	{
		Vertex v = vertices[meshlet.vertOffset + gtid];
		outVerts[gtid].position = mul(ViewProj, mul(World, float4(v.position, 1)));
		outVerts[gtid].normal = normalize(mul((float3x3)World, v.normal));
		outVerts[gtid].meshletIndex = gtid;
	}

	if (gtid < meshlet.triCount)
	{
		outTriIndices[gtid] = uint3(indices[meshlet.triOffset + gtid], indices[meshlet.triOffset + gtid + 1], indices[meshlet.triOffset + gtid + 2]);
	}
}