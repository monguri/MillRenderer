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
#else // #ifdef USE_DYNAMIC_RESOURCE
ConstantBuffer<Material> CbMaterial : register(b1);
Texture2D BaseColorMap : register(t0);
#endif //#ifdef USE_DYNAMIC_RESOURCE

SamplerState BaseColorSmp : register(s0);

void main(VSOutput input)
{
#ifdef USE_DYNAMIC_RESOURCE
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];
	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
#endif //#ifdef USE_DYNAMIC_RESOURCE
	float4 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
}
