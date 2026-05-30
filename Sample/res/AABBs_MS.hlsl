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
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_MESH)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_MESH)"\
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
")"\

// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;
static const uint EACH_MESH_DESCRIPTOR_COUNT = 6;

struct MeshesDescHeapIndices
{
	//uint CbMesh[MAX_MESH_COUNT];
	//uint SbVertexBuffer[MAX_MESH_COUNT];
	//uint SbMeshletBuffer[MAX_MESH_COUNT];
	//uint SbMeshletVerticesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletTrianglesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletAABBInfosBuffer[MAX_MESH_COUNT];

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[MAX_MESH_COUNT * EACH_MESH_DESCRIPTOR_COUNT / 4];
};

static const uint CbMeshBaseIdx = 0;
static const uint SbVertexBufferBaseIdx = CbMeshBaseIdx  + MAX_MESH_COUNT;
static const uint SbMeshletBufferBaseIdx = SbVertexBufferBaseIdx + MAX_MESH_COUNT;
static const uint SbMeshletVerticesBufferBaseIdx = SbMeshletBufferBaseIdx + MAX_MESH_COUNT;
static const uint SbMeshletTrianglesBufferBaseIdx = SbMeshletVerticesBufferBaseIdx + MAX_MESH_COUNT;
static const uint SbMeshletAABBInfosBufferBaseIdx = SbMeshletTrianglesBufferBaseIdx + MAX_MESH_COUNT;

struct VSOutput
{
	float4 Position : SV_Position;
	uint InstanceID : INSTANCE_ID;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

struct Transform
{
	float4x4 ViewProj;
};

struct MeshletMeshMaterial
{
	uint MeshIdx;
	uint MaterialIdx;
	uint LocalMeshletIdx;
	uint bMasked;
};

struct AABB
{
	float3 Center;
	float3 HalfExtent;
};

ConstantBuffer<MeshesDescHeapIndices> CbMeshesDescHeapIndices : register(b0);
ConstantBuffer<Transform> CbTransform : register(b1);
ByteAddressBuffer BbDrawMeshletIndices : register(t0);
StructuredBuffer<MeshletMeshMaterial> SbMeshletMeshMaterialTable : register(t1);
StructuredBuffer<float3> SbUnitCubeVertices : register(t2);
StructuredBuffer<uint> SbUnitCubeIndices : register(t3);

uint GetDescHeapIndex(uint meshIdx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMeshesDescHeapIndices.Indices[meshIdx >> 2][meshIdx & 0b11];
	//uint ret = CbMeshletsDescHeapIndices.Indices[meshIdx / 4][meshIdx % 4];
	return ret;
}

static const uint CUBE_VERTEX_COUNT = 24;
static const uint CUBE_TRIANGLE_COUNT = 12;

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
	uint meshletIdx = BbDrawMeshletIndices.Load(gid * 4);
	MeshletMeshMaterial meshMaterial = SbMeshletMeshMaterialTable[meshletIdx];
	uint meshIdx = meshMaterial.MeshIdx;

	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[GetDescHeapIndex(CbMeshBaseIdx + meshIdx)];
	StructuredBuffer<AABB> SbMeshletAABBInfos = ResourceDescriptorHeap[GetDescHeapIndex(SbMeshletAABBInfosBufferBaseIdx + meshIdx)];
	AABB aabb = SbMeshletAABBInfos[meshMaterial.LocalMeshletIdx];

	SetMeshOutputCounts(CUBE_VERTEX_COUNT, CUBE_TRIANGLE_COUNT);

	if (gtid < CUBE_VERTEX_COUNT)
	{
		float3 localPos = SbUnitCubeVertices[gtid];
		localPos *= aabb.HalfExtent;
		localPos += aabb.Center;
		float4 worldPos = mul(CbMesh.World, float4(localPos, 1));
		float4 projPos = mul(CbTransform.ViewProj, worldPos);

		VSOutput v;
		v.Position = projPos;
		v.InstanceID = meshletIdx;
		outVerts[gtid] = v;
	}

	if (gtid < CUBE_TRIANGLE_COUNT)
	{
		uint triBaseIdx = gtid * 3;
		outTriIndices[gtid] = uint3(
			SbUnitCubeIndices[triBaseIdx],
			SbUnitCubeIndices[triBaseIdx + 1],
			SbUnitCubeIndices[triBaseIdx + 2]
		);
	}
}