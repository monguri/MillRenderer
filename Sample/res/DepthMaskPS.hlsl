#define USE_DYNAMIC_RESOURCE

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
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
};

#ifdef USE_DYNAMIC_RESOURCE
cbuffer CbRootConst1 : register(b1)
{
	uint CbMaterialDescIndex;
}

cbuffer CbRootConst10 : register(b10)
{
	uint BaseColorMapDescIndex;
}
#else // #ifdef USE_DYNAMIC_RESOURCE
ConstantBuffer<Material> CbMaterial : register(b1);
Texture2D BaseColorMap : register(t0);
#endif //#ifdef USE_DYNAMIC_RESOURCE

SamplerState BaseColorSmp : register(s0);

void main(VSOutput input)
{
#ifdef USE_DYNAMIC_RESOURCE
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbMaterialDescIndex];
	Texture2D BaseColorMap = ResourceDescriptorHeap[BaseColorMapDescIndex];
#endif //#ifdef USE_DYNAMIC_RESOURCE
	float4 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
}
