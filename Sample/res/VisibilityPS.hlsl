struct MSOutput
{
	float4 Position : SV_Position;
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshletID : MESHLET_ID;
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
	uint CbCamera;
	uint CbMaterial;
	uint CbDirLight;
	uint CbPointLight1;
	uint CbPointLight2;
	uint CbPointLight3;
	uint CbPointLight4;
	uint CbSpotLight1;
	uint CbSpotLight2;
	uint CbSpotLight3;
	uint BaseColorMap;
	uint MetallicRoughnessMap;
	uint NormalMap;
	uint EmissiveMap;
	uint AOMap;
	uint DirLightShadowMap;
	uint SpotLight1ShadowMap;
	uint SpotLight2ShadowMap;
	uint SpotLight3ShadowMap;
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b1);

uint2 main(MSOutput input) : SV_TARGET
{
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	// MaterialID‚ÆMeshletID‚Í16bit‚¸‚Â‚ÉŽû‚Ü‚é‘z’è
	return uint2((CbMaterial.MaterialID << 16) | (input.MeshletID & 0xffff), input.PrimitiveID);
}