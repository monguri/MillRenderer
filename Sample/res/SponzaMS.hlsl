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
	float3 DirLightShadowCoord : TEXCOORD2;
	float3 SpotLight1ShadowCoord : TEXCOORD3;
	float3 SpotLight2ShadowCoord : TEXCOORD4;
	float3 SpotLight3ShadowCoord : TEXCOORD5;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

cbuffer CbTransform : register(b0)
{
	float4x4 ViewProj;
}

cbuffer CbMesh : register(b1)
{
	float4x4 World;
}

StructuredBuffer<VSInput> vertexBuffer : register(t0);
StructuredBuffer<meshopt_Meshlet> meshlets : register(t1);
StructuredBuffer<uint> meshletsVertices : register(t2);
#if 0
ByteAddressBuffer meshletsTriangles : register(t3);
#else
StructuredBuffer<uint> meshletsTriangles : register(t3);
#endif

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
		// TODO:ShadowCoordも追加
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