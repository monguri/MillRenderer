struct MSOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD;
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshID : MESH_ID;
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
		discard;
	}
#endif

	// MaterialID‚ÆMeshID‚Í16bit‚¸‚Â‚ÉŽû‚Ü‚é‘z’è
	return uint2((CbMaterial.MaterialID << 16) | (input.MeshID & 0xffff), input.PrimitiveID);
}