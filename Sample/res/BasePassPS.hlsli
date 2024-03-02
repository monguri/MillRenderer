#include "BRDF.hlsli"

// referenced UE.
static const float DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE = 6353.17f;
static const float DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS = 0.1f;

#define USE_MANUAL_PCF_FOR_SHADOW_MAP
//#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
//#define SINGLE_SAMPLE_SHADOW_MAP

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

// Tokuyoshi, Y., and Kaplanyan, A. S. 2021. Stable Geometric Specular Antialiasing with Projected-Space NDF Filtering. JCGT, 10, 2, 31-58.
// https://cedil.cesa.or.jp/cedil_sessions/view/2395
float IsotropicNDFFiltering(float3 normal, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float SIGMA2 = 0.5f * (1.0f / F_PI);
	float KAPPA = 0.18f;

	float3 dndu = ddx(normal);
	float3 dndv = ddy(normal);

	float kernel = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
	float clampedKernel = min(kernel, KAPPA);

	float filteredAlphaSq = saturate(alphaSq + clampedKernel);
	float filteredRoughness = sqrt(sqrt(filteredAlphaSq));
	return filteredRoughness;
}

// referenced UE ShadowFilteringCommon.ush
float4 CalculateOcclusion(float4 shadowMapDepth, float sceneDepth, float transitionScale)
{
	// The standard comparison is SceneDepth < ShadowmapDepth
	// Using a soft transition based on depth difference
	// Offsets shadows a bit but reduces self shadowing artifacts considerably

	// Unoptimized Math: saturate((Settings.SceneDepth - ShadowmapDepth) * TransitionScale + 1);
	// Rearranged the math so that per pixel constants can be optimized from per sample constants.
	return saturate((sceneDepth - shadowMapDepth) * transitionScale + 1);
}

// horizontal PCF, input 6x2
float2 HorizontalPCF5x2(float2 fraction, float4 values00, float4 values20, float4 values40)
{
	float result0;
	float result1;

	result0 = values00.w * (1.0 - fraction.x);
	result1 = values00.x * (1.0 - fraction.x);
	result0 += values00.z;
	result1 += values00.y;
	result0 += values20.w;
	result1 += values20.x;
	result0 += values20.z;
	result1 += values20.y;
	result0 += values40.w;
	result1 += values40.x;
	result0 += values40.z * fraction.x;
	result1 += values40.y * fraction.x;

	return float2(result0, result1);
}

// PCF falloff is overly blurry, apply to get
// a more reasonable look artistically. Should not be applied to
// other passes that target pbr (e.g., raytracing and denoising).
float ApplyPCFOverBlurCorrection(float occlusion)
{
	return occlusion * occlusion;
}

float GetShadowMultiplier(Texture2D ShadowMap, float2 ShadowMapSize, float3 shadowCoord, float transitionScale)
{
#ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
	// referenced UE Manual5x5PCF() of ShadowFilteringCommon.ush
	// high quality, 6x6 samples, using gather4
	float2 texelPos = shadowCoord.xy * ShadowMapSize.x - 0.5f;	// bias to be consistent with texture filtering hardware
	float2 fraction = frac(texelPos);
	// Gather4 samples "at the following locations: (-,+),(+,+),(+,-),(-,-), where the magnitude of the deltas are always half a texel" - DX11 Func. Spec.
	// So we need to offset to the centre of the 2x2 grid we want to sample.
	float2 samplePos = (floor(texelPos) + 1.0f) * ShadowMapSize.y;

	// transisionScale is considered NdotL, but use fixed value at pixel offset.
	// shadowCoord.z is fixed too.
	float result = 0.0f;
	{
		float4 values00 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(-2, -2)), shadowCoord.z, transitionScale);
		float4 values20 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(0, -2)), shadowCoord.z, transitionScale);
		float4 values40 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(2, -2)), shadowCoord.z, transitionScale);

		float2 row0 = HorizontalPCF5x2(fraction, values00, values20, values40);
		result += row0.x * (1.0f - fraction.y) + row0.y;
	}

	{
		float4 values02 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(-2, 0)), shadowCoord.z, transitionScale);
		float4 values22 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(0, 0)), shadowCoord.z, transitionScale);
		float4 values42 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(2, 0)), shadowCoord.z, transitionScale);

		float2 row1 = HorizontalPCF5x2(fraction, values02, values22, values42);
		result += row1.x + row1.y;
	}

	{
		float4 values04 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(-2, 2)), shadowCoord.z, transitionScale);
		float4 values24 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(0, 2)), shadowCoord.z, transitionScale);
		float4 values44 = CalculateOcclusion(ShadowMap.Gather(ShadowSmp, samplePos, int2(2, 2)), shadowCoord.z, transitionScale);

		float2 row2 = HorizontalPCF5x2(fraction, values04, values24, values44);
		result += row2.x + row2.y * fraction.y;
	}

	result /= 25;
	result = ApplyPCFOverBlurCorrection(result);
	return (1 - result);
#else // USE_MANUAL_PCF_FOR_SHADOW_MAP
	#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#ifdef SINGLE_SAMPLE_SHADOW_MAP
		float result = ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy, shadowCoord.z);
		#else // SINGLE_SAMPLE_SHADOW_MAP
		const float Dilation = 2.0f;
		float d1 = Dilation * ShadowMapSize.y * 0.125f;
		float d2 = Dilation * ShadowMapSize.y * 0.875f;
		float d3 = Dilation * ShadowMapSize.y * 0.625;
		float d4 = Dilation * ShadowMapSize.y * 0.375;
		float result = (2.0f * ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy, shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(-d2, d1), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(-d1, d2), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(d2, d1), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(d1, d2), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(-d4, d3), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(-d3, d4), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(d4, d3), shadowCoord.z)
			+ ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy + float2(d3, d4), shadowCoord.z)) / 10.0f;
		#endif // SINGLE_SAMPLE_SHADOW_MAP
	#else // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#ifdef SINGLE_SAMPLE_SHADOW_MAP
		float shadowVal = ShadowMap.Sample(ShadowSmp, shadowCoord.xy).x;
		float result = 1.0f;
		if (shadowCoord.z > shadowVal)
		{
			result = 0.0f;
		}
		#else // SINGLE_SAMPLE_SHADOW_MAP
		const float Dilation = 2.0f;
		float d1 = Dilation * ShadowMapSize.y * 0.125f;
		float d2 = Dilation * ShadowMapSize.y * 0.875f;
		float d3 = Dilation * ShadowMapSize.y * 0.625;
		float d4 = Dilation * ShadowMapSize.y * 0.375;
		float result = (2.0f * ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(-d2, d1)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(-d1, d2)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(d2, d1)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(d1, d2)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(-d4, d3)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(-d3, d4)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(d4, d3)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > ShadowMap.Sample(ShadowSmp, shadowCoord.xy + float2(d3, d4)).x) ? 0.0f : 1.0f)) / 10.0f;
		#endif // SINGLE_SAMPLE_SHADOW_MAP
	#endif // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP

	return result * result;
#endif // USE_MANUAL_PCF_FOR_SHADOW_MAP
}

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
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, DirLightShadowMapSize, input.DirLightShadowCoord, transitionScale );
	float3 dirLightReflection = dirLightBRDF * DirLightColor * DirLightIntensity * dirLightShadowMult;

	output.Color.rgb = dirLightReflection;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;
	return output;
}

