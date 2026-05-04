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
", RootConstants(num32BitConstants=1, b0, visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(UAV(u0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(UAV(u1), visibility = SHADER_VISIBILITY_ALL)"\

//TODO: GBufferFromVBufferPS.hlslと共通化できる定数は共通ヘッダに移す
// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;

// CbMesh, SbVertexBuffer, BbDrawMeshletListBuffer, SbMeshletBuffer, SbMeshletVerticesBuffer, SbMeshletTrianglesBuffer, SbMeshletAABBInfosBuffer
static const uint EACH_MESH_DESCRIPTOR_COUNT = 7;

static const uint SbMeshletAABBInfosBufferBaseIdx = (EACH_MESH_DESCRIPTOR_COUNT - 1)* MAX_MESH_COUNT;

struct MeshesDescHeapIndices
{
	//uint CbMesh[MAX_MESH_COUNT];
	//uint SbVertexBuffer[MAX_MESH_COUNT];
	//uint BbDrawMeshletListBuffer[MAX_MESH_COUNT];
	//uint SbMeshletBuffer[MAX_MESH_COUNT];
	//uint SbMeshletVerticesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletTrianglesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletAABBInfosBuffer[MAX_MESH_COUNT];

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[MAX_MESH_COUNT * EACH_MESH_DESCRIPTOR_COUNT / 4];
};

struct RootConstants
{
	uint MeshletCount;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

struct Transform
{
	float4x4 ViewProj;
	float4x4 WorldToDirLightShadowMap;
	float4x4 WorldToSpotLight1ShadowMap;
	float4x4 WorldToSpotLight2ShadowMap;
	float4x4 WorldToSpotLight3ShadowMap;
};

struct Culling
{
	uint bEnableFrustumCulling;
	uint bEnableOcclusionCulling;
	uint bEnableBackFaceCulling;
};

struct AABB
{
	float3 Center;
	float3 HalfExtent;
};

struct MeshletMeshMaterial
{
	uint MeshIdx;
	uint MaterialIdx;
};

ConstantBuffer<RootConstants> CbRootConst : register(b0);
ConstantBuffer<MeshesDescHeapIndices> CbMeshesDescHeapIndices : register(b1);
ConstantBuffer<Transform> CbTransform : register(b2);
ConstantBuffer<Culling> CbCulling : register(b3);
StructuredBuffer<MeshletMeshMaterial> SbMeshletMeshMaterialTable : register(t0);
RWByteAddressBuffer DrawVBufferIndirectArgBB : register(u0);
RWByteAddressBuffer DrawVBufferMeshletListBB : register(u1);

uint GetCbMeshDescHeapIndex(uint meshIdx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMeshesDescHeapIndices.Indices[meshIdx >> 2][meshIdx & 0b11];
	//uint ret = CbMeshesDescHeapIndices.Indices[meshIdx / 4][meshIdx % 4];
	return ret;
}

uint GetSbMeshletAABBInfoDescHeapIndex(uint meshIdx)
{
	uint idx = SbMeshletAABBInfosBufferBaseIdx + meshIdx;

	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMeshesDescHeapIndices.Indices[idx >> 2][idx & 0b11];
	//uint ret = CbMeshesDescHeapIndices.Indices[idx / 4][idx % 4];
	return ret;
}

// InverseZ、InfinitePlane
bool frustumCull(float3 aabbNdcPos[8])
{
	//TODO: AABBがフラスタムを囲むように交差する場合は考慮してない

	// AABBの8頂点のうち、1つでもフラスタム内にあれば交差している
	bool isIntersecting = false;
	for (uint i = 0; i < 8; i++)
	{
		if (aabbNdcPos[i].x < -1.0f || aabbNdcPos[i].x > 1.0f)
		{
			continue;
		}
		
		if (aabbNdcPos[i].y < -1.0f || aabbNdcPos[i].y > 1.0f)
		{
			continue;
		}
		
		if (aabbNdcPos[i].z < 0.0f || aabbNdcPos[i].z > 1.0f)
		{
			continue;
		}

		isIntersecting = true;
	}

	return isIntersecting;
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint meshletIdx : SV_DispatchThreadID)
{
	if (meshletIdx == 0)
	{
		// Y=1,Z=1の引数初期化。cppでClearUavWithUintValue()でやりにくいためここで。
		DrawVBufferIndirectArgBB.Store(4, 1);
		DrawVBufferIndirectArgBB.Store(8, 1);
	}

	if (meshletIdx >= CbRootConst.MeshletCount)
	{
		return;
	}

	MeshletMeshMaterial meshMaterial = SbMeshletMeshMaterialTable[meshletIdx];
	StructuredBuffer<AABB> meshletsAABBInfo = ResourceDescriptorHeap[GetSbMeshletAABBInfoDescHeapIndex(meshMaterial.MeshIdx)];
	AABB aabb = meshletsAABBInfo[meshletIdx];

	float3 vertices[8] =
	{
		aabb.Center + float3(-aabb.HalfExtent.x, -aabb.HalfExtent.y, -aabb.HalfExtent.z),
		aabb.Center + float3(-aabb.HalfExtent.x, -aabb.HalfExtent.y,  aabb.HalfExtent.z),
		aabb.Center + float3(-aabb.HalfExtent.x,  aabb.HalfExtent.y, -aabb.HalfExtent.z),
		aabb.Center + float3(-aabb.HalfExtent.x,  aabb.HalfExtent.y,  aabb.HalfExtent.z),
		aabb.Center + float3( aabb.HalfExtent.x, -aabb.HalfExtent.y, -aabb.HalfExtent.z),
		aabb.Center + float3( aabb.HalfExtent.x, -aabb.HalfExtent.y,  aabb.HalfExtent.z),
		aabb.Center + float3( aabb.HalfExtent.x,  aabb.HalfExtent.y, -aabb.HalfExtent.z),
		aabb.Center + float3( aabb.HalfExtent.x,  aabb.HalfExtent.y,  aabb.HalfExtent.z),
	};

	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[GetCbMeshDescHeapIndex(meshMaterial.MeshIdx)];

	// モデル座標からNDC座標への変換
	for (uint i = 0; i < 8; i++)
	{
		float4 clipPos = mul(CbTransform.ViewProj, mul(CbMesh.World, float4(vertices[i], 1.0f)));
		vertices[i] = clipPos.xyz / clipPos.w;
	}

	bool visible = true;
	if (CbCulling.bEnableFrustumCulling == 1)
	{
		 visible = visible && frustumCull(vertices);
	}

	if (visible)
	{
		uint visibleMeshletIdx;
		DrawVBufferIndirectArgBB.InterlockedAdd(0, 1, visibleMeshletIdx);

		DrawVBufferMeshletListBB.Store(visibleMeshletIdx * 4, meshletIdx);
	}
}