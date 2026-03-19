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
")"\
", RootConstants(num32BitConstants=1, b0, visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(UAV(u0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(UAV(u1), visibility = SHADER_VISIBILITY_ALL)"\

struct RootConstants
{
	uint MeshletCount;
};

struct Transform
{
	float4x4 ViewProj;
	float4x4 WorldToDirLightShadowMap;
	float4x4 WorldToSpotLight1ShadowMap;
	float4x4 WorldToSpotLight2ShadowMap;
	float4x4 WorldToSpotLight3ShadowMap;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

struct AABB
{
	float3 Center;
	float3 HalfExtent;
};

ConstantBuffer<RootConstants> CbRootConst : register(b0);
ConstantBuffer<Transform> CbTransform : register(b1);
ConstantBuffer<Mesh> CbMesh : register(b2);
StructuredBuffer<AABB> SbAABBInfos : register(t0);
RWByteAddressBuffer DrawVBufferIndirectArgBB : register(u0);
RWByteAddressBuffer DrawVBufferMeshletListBB : register(u1);

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
	if (meshletIdx >= CbRootConst.MeshletCount)
	{
		return;
	}

	AABB aabb = SbAABBInfos[meshletIdx];

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

	// モデル座標からNDC座標への変換
	for (uint i = 0; i < 8; i++)
	{
		float4 clipPos = mul(CbTransform.ViewProj, mul(CbMesh.World, float4(vertices[i], 1.0f)));
		vertices[i] = clipPos.xyz / clipPos.w;
	}

	bool visible = frustumCull(vertices);

	if (visible)
	{
		uint visibleMeshletIdx;
		DrawVBufferIndirectArgBB.InterlockedAdd(0, 1, visibleMeshletIdx);

		DrawVBufferMeshletListBB.Store(visibleMeshletIdx * 4, meshletIdx);
	}
}