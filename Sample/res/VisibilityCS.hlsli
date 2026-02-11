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

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// からコードをとってきた
// TODO: GBufferFromVBufferPS>hlslにも同じ関数があるので共通化したい
float3 CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	float3 ret = 0;

	float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	float3 m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	float3 m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	float ddxSum = dot(m_ddx, float3(1,1,1));
	float ddySum = dot(m_ddy, float3(1,1,1));

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);

	ret.x = interpW * (invW[0] + deltaVec.x*m_ddx.x + deltaVec.y*m_ddy.x);
	ret.y = interpW * (0.0f    + deltaVec.x*m_ddx.y + deltaVec.y*m_ddy.y);
	ret.z = interpW * (0.0f    + deltaVec.x*m_ddx.z + deltaVec.y*m_ddy.z);

	return ret;
}

// 外積のz成分にあたる
// 符号はA-Bのエッジに対しA-Cが時計回りなら正、反時計回りなら負
// 絶対値はA-BとA-Cのベクトルの成す平行四辺形の面積。三角形の面積の2倍。
int area2D(int2 a, int2 b, int2 c)
{
	return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

void renderPixel(uint2 pixelPos, float3 baryCentricCrd, VertexData v0, VertexData v1, VertexData v2, PrimitiveData primData)
{
	// TODO: ここだけグローバルなリソースにアクセスしているしこの中でdiscardしている
#ifdef ALPHA_MODE_MASK
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
	float2 texCoord = v0.TexCoord * baryCentricCrd.x + v1.TexCoord * baryCentricCrd.y + v2.TexCoord * baryCentricCrd.z;
	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, texCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		return;
	}
#endif

	// 重心座標補間は以下を参考にした
	// https://shikihuiku.wordpress.com/2017/05/23/barycentric-coordinates%E3%81%AE%E8%A8%88%E7%AE%97%E3%81%A8perspective-correction-partial-derivative%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6/
	// Inverse Z、Infinite Far PlaneなのでClipSpaceZは実はNear固定である。
	// ClipSpaceW = ViewZである。
	float3 ndcPosZs = float3(
		v0.Position.z * rcp(v0.Position.w),
		v1.Position.z * rcp(v1.Position.w),
		v2.Position.z * rcp(v2.Position.w)
	);
	float deviceZ = dot(ndcPosZs, baryCentricCrd);

	if (!(deviceZ >= 0 && deviceZ <= 1))
	{
		// Inverse Z、Infinite Far Planeによるクリッピング
		// InterlockedMaxは負になるとasuint(float)が正の値に勝ってしまうので正の値前提というのもある
		return;
	}

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
	//TODO: AlphaMaskが必要

	float3 ndcPos0 = v0.Position.xyz / v0.Position.w;
	float3 ndcPos1 = v1.Position.xyz / v1.Position.w;
	float3 ndcPos2 = v2.Position.xyz / v2.Position.w;

	// ピクセル座標は本来はNDCとはY軸が逆だが今回は後で調整する
	uint2 pixelPos0 = uint2(((ndcPos0.xy * 0.5f) + 0.5f) * uint2(screenWidth, screenHeight));
	uint2 pixelPos1 = uint2(((ndcPos1.xy * 0.5f) + 0.5f) * uint2(screenWidth, screenHeight));
	uint2 pixelPos2 = uint2(((ndcPos2.xy * 0.5f) + 0.5f) * uint2(screenWidth, screenHeight));

	uint2 minBB = min(pixelPos0, min(pixelPos1, pixelPos2));
	uint2 maxBB = max(pixelPos0, max(pixelPos1, pixelPos2));
	
	// clampではダメ。Triangleが画面範囲外のときにループが回らないように
	minBB = max(minBB, uint2(0, 0));
	maxBB = min(maxBB, uint2(screenWidth - 1, screenHeight - 1));
	
	for (uint y = minBB.y; y <= maxBB.y; y++)
	{
		for (uint x = minBB.x; x <= maxBB.x; x++)
		{
			uint2 pixelPos = uint2(x, y);
#if 1
			int area0 = area2D(pixelPos1, pixelPos2, pixelPos);
			int area1 = area2D(pixelPos2, pixelPos0, pixelPos);
			int area2 = area2D(pixelPos0, pixelPos1, pixelPos);
			int totalArea = area2D(pixelPos0, pixelPos1, pixelPos2);

			// TODO: とりあえず3頂点が同じピクセルにある場合は除外している
			if (area0 >= 0 && area1 >= 0 && area2 >= 0 && totalArea > 0)
			{
				// Y軸反転
				pixelPos = uint2(pixelPos.x, screenHeight - 1 - pixelPos.y);
				float3 baryCentricCrd = float3(area0, area1, area2) / totalArea;
				renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
			}
#else
			// [-1,1]x[-1,1]
			float2 screenPos = (float2(pixelPos) + 0.5f) / float2(screenWidth, screenHeight) * 2 - 1;

			float3 baryCentricCrd = CalcFullBary(v0.Position, v1.Position, v2.Position, screenPos, float2(screenWidth, screenHeight));
			// ピクセルが三角形の内側にあれば書き込む
			if (all(baryCentricCrd) >= 0)
			{
				renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
			}
#endif
		}
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

		VertexData v0 = outVerts[meshletsTriangles[triBaseIdx + 0]];
		VertexData v1 = outVerts[meshletsTriangles[triBaseIdx + 1]];
		VertexData v2 = outVerts[meshletsTriangles[triBaseIdx + 2]];

		PrimitiveData primData;
		primData.MeshIdx = CbMesh.MeshIdx;
		primData.MeshletIdx = gid;
		primData.TriangleIdx = gtid;

		softwareRasterize(v0, v1, v2, primData, CbDrawVBufferSWRas.Width, CbDrawVBufferSWRas.Height);
	}
}