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

//TODO: BasePassVS.hlsl and BasePassMS.hlsl have different VSInput definitions, need to unify
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

StructuredBuffer<Meshlet> meshlets : register(t0);
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
	Meshlet meshlet = meshlets[gid];
	SetMeshOutputCounts(meshlet.vertCount, meshlet.triCount);

	if (gtid < meshlet.vertCount)
	{
		VSInput input = vertices[meshlet.vertOffset + gtid];

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

	if (gtid < meshlet.triCount)
	{
		outTriIndices[gtid] = uint3(indices[meshlet.triOffset + gtid], indices[meshlet.triOffset + gtid + 1], indices[meshlet.triOffset + gtid + 2]);
	}
}