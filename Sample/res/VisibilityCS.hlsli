// TODO: とりあえずMeshlet、DynamicResourceのときに実装を限定する
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
", RootConstants(num32BitConstants=10, b0, visibility = SHADER_VISIBILITY_ALL)"\
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
", visibility = SHADER_VISIBILITY_ALL"\
")"\

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
	uint TriangleIdx : TRIANGLE_INDEX;
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
	uint CbMaterial;
	uint BaseColorMap;
	uint CbDrawVBufferSWRas;
	uint VBuffer;
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

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
	uint MaterialID;
};

struct DrawVBufferSWRas
{
	int Width;
	int Height;
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b0);
SamplerState AnisotropicWrapSmp : register(s0);

groupshared VertexData outVerts[64];

// 外積のz成分にあたる
// 符号はA-Bのエッジに対しA-Cが時計回りなら正、反時計回りなら負
// 絶対値はA-BとA-Cのベクトルの成す平行四辺形の面積。三角形の面積の2倍。
int area2D(int2 a, int2 b, int2 c)
{
	return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

void renderPixel(uint2 pixelPos, float3 baryCentricCrd, VertexData v0, VertexData v1, VertexData v2, PrimitiveData primData)
{
	// Inverse Z、Infinite Far PlaneだとClipSpaceW = ViewZである。
	float3 invViewZs = float3(
		rcp(v0.Position.w),
		rcp(v1.Position.w),
		rcp(v2.Position.w)
	);

#ifdef ALPHA_MODE_MASK
	// TODO: ここだけグローバルなリソースにアクセスしているしこの中でdiscardしている
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];

	float viewZ = rcp(dot(invViewZs, baryCentricCrd));
	float2 texCoord = (v0.TexCoord * invViewZs.x * baryCentricCrd.x + v1.TexCoord * invViewZs.y * baryCentricCrd.y + v2.TexCoord * invViewZs.z * baryCentricCrd.z) * viewZ;
	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, texCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		return;
	}
#endif

	// 重心座標補間は以下を参考にした
	// https://shikihuiku.wordpress.com/2017/05/23/barycentric-coordinates%E3%81%AE%E8%A8%88%E7%AE%97%E3%81%A8perspective-correction-partial-derivative%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6/
	// Inverse Z、Infinite Far Planeなので全頂点のClipSpaceZはNear固定である。
	float3 ndcPosZs = float3(
		v0.Position.z * invViewZs.x,
		v1.Position.z * invViewZs.y,
		v2.Position.z * invViewZs.z
	);
	float deviceZ = dot(ndcPosZs, baryCentricCrd);

#if 0
	if (!(deviceZ >= 0 && deviceZ <= 1))
	{
		// Inverse Z、Near Plane、Infinite Far Planeによるクリッピング
		// InterlockedMaxは負になるとasuint(float)が正の値に勝ってしまうので正の値前提というのもある
		return;
	}
#else
	//assert(deviceZ >= 0 && deviceZ <= 1);
#endif

#if 1
	RWTexture2D<uint64_t> VBuffer = ResourceDescriptorHeap[CbDescHeapIndices.VBuffer];

	uint2 value;
	value.x = (primData.MeshIdx << 23) | ((primData.MeshletIdx << 7) & 0xffff) | (primData.TriangleIdx & 0x7f);
	value.y = asuint(deviceZ);

	uint64_t packedValue = (uint64_t(value.y) << 32) | uint64_t(value.x);

	// InverseZなのでMaxをとる
	InterlockedMax(VBuffer[pixelPos], packedValue);
#else
	RWTexture2D<uint2> VBuffer = ResourceDescriptorHeap[CbDescHeapIndices.VBuffer];
	uint2 value;
	value.x = 
		(primData.MeshIdx << 23)
		| ((primData.MeshletIdx << 7) & 0xffff)
		| (primData.TriangleIdx & 0x7f);
	value.y = asuint(deviceZ);
	VBuffer[pixelPos] = value;
#endif
}

void softwareRasterize(VertexData v0, VertexData v1, VertexData v2, PrimitiveData primData, uint screenWidth, uint screenHeight)
{
	// https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
	// を参考にしている

#if 1
	// Inverse ZなのでzはNear固定
	float near = v0.Position.z;

	// TODO: カメラの後ろの頂点がある場合はどう扱うべきかわからないのでとりあえずnearクリップより後ろに頂点がある場合は描画しないようにしているが、正しい処理はクリッピングして新しい三角形を作ってラスタライズすることだと思う
	if (v0.Position.w < near && v1.Position.w < near && v2.Position.w < near)
	{
		return;
	}
#else
	//assert(v0.Position.w >= near && v1.Position.w >= near && v2.Position.w >= near);
#endif

	float3 ndcPos0 = v0.Position.xyz / v0.Position.w;
	float3 ndcPos1 = v1.Position.xyz / v1.Position.w;
	float3 ndcPos2 = v2.Position.xyz / v2.Position.w;

	// ピクセル座標は本来はNDCとはY軸が逆だが今回は後で調整する
	int2 pixelPos0 = int2(((ndcPos0.xy * 0.5f) + 0.5f) * int2(screenWidth, screenHeight));
	int2 pixelPos1 = int2(((ndcPos1.xy * 0.5f) + 0.5f) * int2(screenWidth, screenHeight));
	int2 pixelPos2 = int2(((ndcPos2.xy * 0.5f) + 0.5f) * int2(screenWidth, screenHeight));

	int2 minBB = min(pixelPos0, min(pixelPos1, pixelPos2));
	int2 maxBB = max(pixelPos0, max(pixelPos1, pixelPos2));
	
	// clampではダメ。Triangleが画面範囲外のときにループが回らないように
	minBB = max(minBB, int2(0, 0));
	maxBB = min(maxBB, int2(screenWidth - 1, screenHeight - 1));
	
	for (int y = minBB.y; y <= maxBB.y; y++)
	{
		for (int x = minBB.x; x <= maxBB.x; x++)
		{
			int2 pixelPos = int2(x, y);
			int area0 = area2D(pixelPos1, pixelPos2, pixelPos);
			int area1 = area2D(pixelPos2, pixelPos0, pixelPos);
			int area2 = area2D(pixelPos0, pixelPos1, pixelPos);
			int totalArea = area2D(pixelPos0, pixelPos1, pixelPos2);

			if (area0 >= 0 && area1 >= 0 && area2 >= 0
				// バックフェイスカリング
				// TODO: とりあえず3頂点が同じピクセルにある場合は除外している
				&& totalArea > 0)
			{
				// Y軸反転
				pixelPos = int2(pixelPos.x, screenHeight - 1 - pixelPos.y);
				float3 baryCentricCrd = float3(area0, area1, area2) / totalArea;
				renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
			}
#if 0
			else if (area0 <= 0 && area1 <= 0 && area2 <= 0
				// twoside
				// TODO: とりあえず3頂点が同じピクセルにある場合は除外している
				&& totalArea < 0)
			{
				// Y軸反転
				pixelPos = int2(pixelPos.x, screenHeight - 1 - pixelPos.y);
				float3 baryCentricCrd = float3(area0, area1, area2) / totalArea;
				renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
			}
#endif
		}
	}
}

static const uint CLIP_RESULT_OUTSIDE = 0;
static const uint CLIP_RESULT_INSIDE_1_VERTEX = 1;
static const uint CLIP_RESULT_INSIDE_2_VERTEX = 2;
static const uint CLIP_RESULT_INSIDE_3_VERTEX = 3;

struct ClipSpaceTriangle
{
	VertexData v0;
	VertexData v1;
	VertexData v2;
};

VertexData calculateNewVertexDataOnNearPlane(VertexData insideVtx, VertexData outsideVertex, float near)
{
	float t = (near - insideVtx.Position.w) / (outsideVertex.Position.w - insideVtx.Position.w);

	VertexData result;
	result.Position.xy = insideVtx.Position.xy + t * (outsideVertex.Position.xy - insideVtx.Position.xy);
	result.Position.z = near;
	result.Position.w = near;

	result.TexCoord = insideVtx.TexCoord + t * (outsideVertex.TexCoord - insideVtx.TexCoord);
	return result;
}

uint nearClip(in ClipSpaceTriangle origTri, in float near, out ClipSpaceTriangle newTri1, out ClipSpaceTriangle newTri2)
{
	bool isV0Inside = origTri.v0.Position.w >= near;
	bool isV1Inside = origTri.v1.Position.w >= near;
	bool isV2Inside = origTri.v2.Position.w >= near;

	uint insideCount = (isV0Inside ? 1 : 0) + (isV1Inside ? 1 : 0) + (isV2Inside ? 1 : 0);
	if (insideCount == 3)
	{
		newTri1 = origTri;
		return CLIP_RESULT_INSIDE_3_VERTEX;
	}
	else if (insideCount == 2)
	{
		VertexData insideVtx0, insideVtx1, outsideVtx;

		if (!isV2Inside) // isV0Inside && isV1Inside
		{
			insideVtx0 = origTri.v0;
			insideVtx1 = origTri.v1;
			outsideVtx = origTri.v2;
		}
		else if (!isV0Inside) // isV1Inside && isV2Inside
		{
			insideVtx0 = origTri.v1;
			insideVtx1 = origTri.v2;
			outsideVtx = origTri.v0;
		}
		else // if (isV2Inside && isV0Inside) i.e. !isV1Inside
		{
			insideVtx0 = origTri.v2;
			insideVtx1 = origTri.v0;
			outsideVtx = origTri.v1;
		}

		VertexData newVtx0 = calculateNewVertexDataOnNearPlane(insideVtx1, outsideVtx, near);
		VertexData newVtx1 = calculateNewVertexDataOnNearPlane(insideVtx0, outsideVtx, near);

		newTri1.v0 = insideVtx0;
		newTri1.v1 = insideVtx1;
		newTri1.v2 = newVtx0;

		newTri2.v0 = insideVtx0;
		newTri2.v1 = newVtx0;
		newTri2.v2 = newVtx1;
		return CLIP_RESULT_INSIDE_2_VERTEX;
	}
	else if (insideCount == 1)
	{
		// クリップして新しい三角形を作る
		VertexData insideVtx, outsideVtx0, outsideVtx1;

		if (isV0Inside)
		{
			insideVtx = origTri.v0;
			outsideVtx0 = origTri.v1;
			outsideVtx1 = origTri.v2;
		}
		else if (isV1Inside)
		{
			insideVtx = origTri.v1;
			outsideVtx0 = origTri.v2;
			outsideVtx1 = origTri.v0;
		}
		else // if (isV2Inside)
		{
			insideVtx = origTri.v2;
			outsideVtx0 = origTri.v0;
			outsideVtx1 = origTri.v1;
		}

		VertexData newVtx0 = calculateNewVertexDataOnNearPlane(insideVtx, outsideVtx0, near);
		VertexData newVtx1 = calculateNewVertexDataOnNearPlane(insideVtx, outsideVtx1, near);

		newTri1.v0 = insideVtx;
		newTri1.v1 = newVtx0;
		newTri1.v2 = newVtx1;
		return CLIP_RESULT_INSIDE_1_VERTEX;
	}
	else // insideCount == 0
	{
		return CLIP_RESULT_OUTSIDE;
	}
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID
)
{
	ConstantBuffer<Transform> CbTransform = ResourceDescriptorHeap[CbDescHeapIndices.CbTransform];
	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[CbDescHeapIndices.CbMesh];

	StructuredBuffer<VSInput> vertexBuffer = ResourceDescriptorHeap[CbDescHeapIndices.SbVertexBuffer];
	StructuredBuffer<meshopt_Meshlet> meshlets = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshlets];
	StructuredBuffer<uint> meshletsVertices = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletVertices];
	StructuredBuffer<uint> meshletsTriangles = ResourceDescriptorHeap[CbDescHeapIndices.SbMeshletTriangles];
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	ConstantBuffer<DrawVBufferSWRas> CbDrawVBufferSWRas = ResourceDescriptorHeap[CbDescHeapIndices.CbDrawVBufferSWRas];

	meshopt_Meshlet meshlet = meshlets[gid];

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

	GroupMemoryBarrierWithGroupSync();

	if (gtid < meshlet.TriCount)
	{
		uint triBaseIdx = meshlet.TriOffset + gtid * 3;

		ClipSpaceTriangle origTri;
		origTri.v0 = outVerts[meshletsTriangles[triBaseIdx + 0]];
		origTri.v1 = outVerts[meshletsTriangles[triBaseIdx + 1]];
		origTri.v2 = outVerts[meshletsTriangles[triBaseIdx + 2]];

		PrimitiveData primData;
		primData.MeshIdx = CbMesh.MeshIdx;
		primData.MeshletIdx = gid;
		primData.TriangleIdx = gtid;

		// Inverse ZなのでzはNear固定
		float near = origTri.v0.Position.z;

		ClipSpaceTriangle newTri1, newTri2;
		uint clipResult = nearClip(origTri, near, newTri1, newTri2);

		switch (clipResult)
		{
		case CLIP_RESULT_OUTSIDE:
			return;
		case CLIP_RESULT_INSIDE_1_VERTEX:
			softwareRasterize(newTri1.v0, newTri1.v1, newTri1.v2, primData, CbDrawVBufferSWRas.Width, CbDrawVBufferSWRas.Height);
			return;
		case CLIP_RESULT_INSIDE_2_VERTEX:
			softwareRasterize(newTri1.v0, newTri1.v1, newTri1.v2, primData, CbDrawVBufferSWRas.Width, CbDrawVBufferSWRas.Height);
			softwareRasterize(newTri2.v0, newTri2.v1, newTri2.v2, primData, CbDrawVBufferSWRas.Width, CbDrawVBufferSWRas.Height);
			return;
		case CLIP_RESULT_INSIDE_3_VERTEX:
			softwareRasterize(origTri.v0, origTri.v1, origTri.v2, primData, CbDrawVBufferSWRas.Width, CbDrawVBufferSWRas.Height);
			return;
		default:
			// ここには来ないはず
			// assert(false);
			return;
		}
	}
}