struct MSOutput
{
	float4 Position : SV_Position;
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshID : MESH_ID;
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
};

ConstantBuffer<DescHeapIndices> CbDescHeapIndices : register(b1);

uint2 main(MSOutput input) : SV_TARGET
{
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	// MaterialID‚ÆMeshID‚Í16bit‚¸‚Â‚ÉŽû‚Ü‚é‘z’è
	return uint2((CbMaterial.MaterialID << 16) | (input.MeshID & 0xffff), input.PrimitiveID);
}