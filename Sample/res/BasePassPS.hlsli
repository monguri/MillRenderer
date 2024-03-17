#define USE_MANUAL_PCF_FOR_SHADOW_MAP
//#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
//#define SINGLE_SAMPLE_SHADOW_MAP

#include "ShadowMap.hlsli"
#include "BRDF.hlsli"

// referenced UE.
static const float DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE = 6353.17f;
static const float DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS = 0.1f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
	float3 DirLightShadowCoord : TEXCOORD2;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
};

cbuffer CbCamera : register(b0)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbMaterial : register(b1)
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float AlphaCutoff;
};

cbuffer CbDirectionalLight : register(b2)
{
	float3 DirLightColor: packoffset(c0);
	float DirLightIntensity: packoffset(c0.w);
	float3 DirLightForward : packoffset(c1);
	float2 DirLightShadowMapSize : packoffset(c2); // x is pixel size, y is texel size on UV.
};

Texture2D BaseColorMap : register(t0);
Texture2D MetallicRoughnessMap : register(t1);
Texture2D NormalMap : register(t2);
SamplerState AnisotropicWrapSmp : register(s0);

Texture2D DirLightShadowMap : register(t3);

#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s1);
#else
SamplerState ShadowSmp : register(s1);
#endif

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;

	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
#ifdef ALPHA_MODE_MASK
	if (baseColor.a < AlphaCutoff)
	{
		discard;
	}
#endif

	baseColor.rgb *= BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.Sample(AnisotropicWrapSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * MetallicFactor;
	float roughness = metallicRoughness.y * RoughnessFactor;

	float3 N = NormalMap.Sample(AnisotropicWrapSmp, input.TexCoord).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(input.InvTangentBasis, N);
	float3 V = normalize(CameraPosition - input.WorldPos);
	float NV = saturate(dot(N, V));

	// directional light
	float3 dirLightL = normalize(-DirLightForward);
	float3 dirLightH = normalize(V + dirLightL);
	float dirLightVH = saturate(dot(V, dirLightH));
	float dirLightNH = saturate(dot(N, dirLightH));
	float dirLightNL = saturate(dot(N, dirLightL));
	float3 dirLightBRDF = ComputeBRDF
	(
		baseColor.rgb,
		metallic,
		roughness,
		dirLightVH,
		dirLightNH,
		NV,
		dirLightNL 
	);

	float transitionScale = DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS, 1, dirLightNL);
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, ShadowSmp, DirLightShadowMapSize, input.DirLightShadowCoord, transitionScale );
	float3 dirLightReflection = dirLightBRDF * DirLightColor * DirLightIntensity * dirLightShadowMult;

	output.Color.rgb = dirLightReflection;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;
	return output;
}

