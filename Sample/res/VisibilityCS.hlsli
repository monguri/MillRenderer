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

	float4 csPos = v0.Position * baryCentricCrd.x + v1.Position * baryCentricCrd.y + v2.Position * baryCentricCrd.z;
	float SV_PositionZ = csPos.z / csPos.w;

#if 1
	RWTexture2D<uint64_t> VBuffer = ResourceDescriptorHeap[CbDescHeapIndices.VBuffer];
	uint2 value;
	value.x = (primData.MeshIdx << 23) | ((primData.MeshletIdx << 7) & 0xffff) | (primData.TriangleIdx & 0x7f);
	value.y = asuint(SV_PositionZ);

	uint64_t packedValue = (uint64_t(value.y) << 32) | uint64_t(value.x);

	// ReverseZなのでMaxをとる
	InterlockedMax(VBuffer[pixelPos], packedValue);
#else
	RWTexture2D<uint2> VBuffer = ResourceDescriptorHeap[CbDescHeapIndices.VBuffer];
	uint2 value;
	value.x = 
		(primData.MeshIdx << 23)
		| ((primData.MeshletIdx << 7) & 0xffff)
		| (primData.TriangleIdx & 0x7f);
	value.y = asuint(SV_PositionZ);
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

	uint2 pixelPos0 = uint2(((ndcPos0.xy * float2(0.5f, -0.5f)) + 0.5f) * uint2(screenWidth, screenHeight));
	uint2 pixelPos1 = uint2(((ndcPos1.xy * float2(0.5f, -0.5f)) + 0.5f) * uint2(screenWidth, screenHeight));
	uint2 pixelPos2 = uint2(((ndcPos2.xy * float2(0.5f, -0.5f)) + 0.5f) * uint2(screenWidth, screenHeight));

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
			int area0 = area2D(pixelPos1, pixelPos2, pixelPos);
			int area1 = area2D(pixelPos2, pixelPos0, pixelPos);
			int area2 = area2D(pixelPos0, pixelPos1, pixelPos);

			// ピクセルが三角形の内側にあれば書き込む
			if (area0 >= 0 && area1 >= 0 && area2 >= 0)
			{
				if (area0 == 0 && area1 == 0 && area2 == 0)
				{
					// 1ピクセルだけの三角形の場合
					float3 baryCentricCrd = float3(1, 0, 0);
					renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
				}
				else
				{
					//TODO: ラスタライザの重心座標ってこんな風に2Dから決めるのが本当に正しいのか？
					// Perspectiveも入ってるのに
					float totalArea = float(area0 + area1 + area2);
					float3 baryCentricCrd = float3(area0, area1, area2) / totalArea;
					renderPixel(pixelPos, baryCentricCrd, v0, v1, v2, primData);
				}
			}
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