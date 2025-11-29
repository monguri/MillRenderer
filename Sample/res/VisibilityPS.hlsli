struct MSOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD;
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshIdx : MESH_INDEX;
};

struct Camera
{
	float3 CameraPosition;
	int bDebugViewMeshletCluster;
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

struct DescHeapIndices
{
	uint CbMaterial;
	uint BaseColorMap;
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b1);
SamplerState AnisotropicWrapSmp : register(s0);

uint2 main(MSOutput input) : SV_TARGET
{
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];

#ifdef ALPHA_MODE_MASK
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		// 毎フレームINVALID_VISIBILITYでVisibilityはクリアしてるのでその値のままになる
		discard;
	}
#endif

	// MaterialIDとMeshIDは16bitずつに収まる想定
	return uint2((CbMaterial.MaterialID << 16) | (input.MeshIdx & 0xffff), input.PrimitiveID);
}