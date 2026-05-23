// C++側の定義と値の一致が必要
static const uint MAX_MATERIAL_COUNT = 256;

static const uint EACH_MATERIAL_DESCRIPTOR_COUNT = 6;

struct MaterialsDescHeapIndices
{
	//uint CbMaterial[MAX_MATERIAL_COUNT];
	//uint BaseColorMap[MAX_MATERIAL_COUNT];
	//uint MetallicRoughnessMap[MAX_MATERIAL_COUNT];
	//uint NormalMap[MAX_MATERIAL_COUNT];
	//uint EmissiveMap[MAX_MATERIAL_COUNT];
	//uint AOMap[MAX_MATERIAL_COUNT];

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[MAX_MATERIAL_COUNT * EACH_MATERIAL_DESCRIPTOR_COUNT / 4];
};

struct VSOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD;
	uint MeshletIdx : MESHLET_INDEX;
};

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	uint bAlphaMask;
	float AlphaCutoff;
	uint bExistEmissiveTex;
	uint bExistAOTex;
};

struct MeshletMeshMaterial
{
	uint MeshIdx;
	uint MaterialIdx;
	uint LocalMeshletIdx;
	uint bMasked;
};

ConstantBuffer<MaterialsDescHeapIndices> CbMaterialsDescHeapIndices : register(b0);
StructuredBuffer<MeshletMeshMaterial> SbMeshletMeshMaterialTable : register(t0);
SamplerState AnisotropicWrapSmp : register(s0);

static const uint CbMaterialBaseIdx = 0;
static const uint BaseColorMapBaseIdx = CbMaterialBaseIdx + MAX_MATERIAL_COUNT;
static const uint MetallicRoughnessMapBaseIdx = BaseColorMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint NormalMapBaseIdx = MetallicRoughnessMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint EmissiveMapBaseIdx = NormalMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint AOMapBaseIdx = EmissiveMapBaseIdx + MAX_MATERIAL_COUNT;

uint GetDescHeapIndex(uint matIdx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMaterialsDescHeapIndices.Indices[matIdx >> 2][matIdx & 0b11];
	//uint ret = CbMaterialsDescHeapIndices.Indices[matIdx / 4][matIdx % 4];
	return ret;
}

void main(VSOutput input)
{
	uint meshletIdx = input.MeshletIdx;
	MeshletMeshMaterial meshMaterial = SbMeshletMeshMaterialTable[meshletIdx];
	uint matIdx = meshMaterial.MaterialIdx;

	if (meshMaterial.bMasked == 1)
	{
		ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[GetDescHeapIndex(CbMaterialBaseIdx + matIdx)];
		Texture2D BaseColorMap = ResourceDescriptorHeap[GetDescHeapIndex(BaseColorMapBaseIdx + matIdx)];
		float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
		if (baseColor.a < CbMaterial.AlphaCutoff)
		{
			discard;
		}
	}
}