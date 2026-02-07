struct MSOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD;
	uint MeshIdx : MESH_INDEX;
	uint MeshletIdx : MESHLET_INDEX;
	uint TriangleIdx : TRIANGLE_INDEX;
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
#ifdef ALPHA_MODE_MASK
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
#endif

	// マテリアルは現在はMaskedとOpaqueの2種類のみでそれも後段では使わないので
	// VBufferには記録しない
	return uint2(
		// TriangleIdxはMeshlet内で最大126個なので7bit。 MeshletIdxは残り25bitのうち16bit与える。
		// MeshIdxは残り9bitで512個まで。
		// TODO:本来はグローバルにMeshletをDB管理することでMeshのMeshlet数の不均等を吸収したい
		(input.MeshIdx << 23)
		| ((input.MeshletIdx << 7) & 0xffff)
		| (input.TriangleIdx & 0x7f),
		// HW Rasterizerではデプステストはデプスバッファで行うのでyには何も入れない
		// TODO: HWRas版でも64bit使っているのは無駄
		0
	);
}