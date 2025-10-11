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
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_MESH)"\

//TODO:Ç¢ÇÎÇ¢ÇÎÇ»VS/MSÇ∆íËã`Ç™èÁí∑
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
	float4 CurClipPos : CUR_CLIP_POSITION;
	float4 PrevClipPos : PREV_CLIP_POSITION;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

cbuffer CbObjectVelocity : register(b0)
{
	float4x4 CurWVPWithJitter : packoffset(c0);
	float4x4 CurWVPNoJitter : packoffset(c4);
	float4x4 PrevWVPNoJitter : packoffset(c8);
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

		VSOutput output = (VSOutput)0;

		float4 localPos = float4(input.Position, 1.0f);
		output.Position = mul(CurWVPWithJitter, localPos);
		output.CurClipPos = mul(CurWVPNoJitter, localPos);
		output.PrevClipPos = mul(PrevWVPNoJitter, localPos);

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