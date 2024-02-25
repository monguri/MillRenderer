#include "BRDF.hlsli"

#ifndef MIN_DIST
#define MIN_DIST (0.01)
#endif // MIN_DIST

// referenced UE.
static const float SHADOW_SOFT_TRANSITION_SCALE = 6353.17f;
static const float PROJECTION_DEPTH_BIAS = 0.1f;

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
	float3 SpotLight1ShadowCoord : TEXCOORD3;
	float3 SpotLight2ShadowCoord : TEXCOORD4;
	float3 SpotLight3ShadowCoord : TEXCOORD5;
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

// TODO:Use ConstantBuffer<>
cbuffer CbPointLight1 : register(b3)
{
	float3 PointLight1Position : packoffset(c0);
	float PointLight1InvSqrRadius : packoffset(c0.w);
	float3 PointLight1Color : packoffset(c1);
	float PointLight1Intensity : packoffset(c1.w);
};

cbuffer CbPointLight2 : register(b4)
{
	float3 PointLight2Position : packoffset(c0);
	float PointLight2InvSqrRadius : packoffset(c0.w);
	float3 PointLight2Color: packoffset(c1);
	float PointLight2Intensity: packoffset(c1.w);
};

cbuffer CbPointLight3 : register(b5)
{
	float3 PointLight3Position : packoffset(c0);
	float PointLight3InvSqrRadius : packoffset(c0.w);
	float3 PointLight3Color : packoffset(c1);
	float PointLight3Intensity : packoffset(c1.w);
};

cbuffer CbPointLight4 : register(b6)
{
	float3 PointLight4Position : packoffset(c0);
	float PointLight4InvSqrRadius : packoffset(c0.w);
	float3 PointLight4Color : packoffset(c1);
	float PointLight4Intensity : packoffset(c1.w);
};

cbuffer CbSpotLight1 : register(b7)
{
	float3 SpotLight1Position : packoffset(c0);
	float SpotLight1InvSqrRadius : packoffset(c0.w);
	float3 SpotLight1Color : packoffset(c1);
	float SpotLight1Intensity : packoffset(c1.w);
	float3 SpotLight1Forward : packoffset(c2);
	float SpotLight1AngleScale : packoffset(c2.w);
	float SpotLight1AngleOffset : packoffset(c3);
	int SpotLight1Type : packoffset(c3.y);
	float2 SpotLight1ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

cbuffer CbSpotLight2 : register(b8)
{
	float3 SpotLight2Position : packoffset(c0);
	float SpotLight2InvSqrRadius : packoffset(c0.w);
	float3 SpotLight2Color : packoffset(c1);
	float SpotLight2Intensity : packoffset(c1.w);
	float3 SpotLight2Forward : packoffset(c2);
	float SpotLight2AngleScale : packoffset(c2.w);
	float SpotLight2AngleOffset : packoffset(c3);
	int SpotLight2Type : packoffset(c3.y);
	float2 SpotLight2ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

cbuffer CbSpotLight3 : register(b9)
{
	float3 SpotLight3Position : packoffset(c0);
	float SpotLight3InvSqrRadius : packoffset(c0.w);
	float3 SpotLight3Color : packoffset(c1);
	float SpotLight3Intensity : packoffset(c1.w);
	float3 SpotLight3Forward : packoffset(c2);
	float SpotLight3AngleScale : packoffset(c2.w);
	float SpotLight3AngleOffset : packoffset(c3);
	int SpotLight3Type : packoffset(c3.y);
	float2 SpotLight3ShadowMapSize : packoffset(c3.z); // x is pixel size, y is texel size on UV.
};

Texture2D BaseColorMap : register(t0);
Texture2D MetallicRoughnessMap : register(t1);
Texture2D NormalMap : register(t2);
SamplerState AnisotropicWrapSmp : register(s0);

Texture2D DirLightShadowMap : register(t3);
Texture2D SpotLight1ShadowMap : register(t4);
Texture2D SpotLight2ShadowMap : register(t5);
Texture2D SpotLight3ShadowMap : register(t6);

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

float SmoothDistanceAttenuation
(
	float squareDistance,
	float invSqrAttRadius
)
{
	float factor = squareDistance * invSqrAttRadius;
	float smoothFactor = saturate(1.0f - factor * factor);
	return smoothFactor * smoothFactor;
}

float GetDistanceAttenuation
(
	float3 unnormalizedLightVector,
	float invSqrAttRadius
)
{
	float sqrDist = dot(unnormalizedLightVector, unnormalizedLightVector);
	float attenuation = 1.0f / max(sqrDist, MIN_DIST * MIN_DIST);
	attenuation *= SmoothDistanceAttenuation(sqrDist, invSqrAttRadius);
	return attenuation;
}

float3 EvaluatePointLight
(
	float3 N,
	float3 worldPos,
	float3 lightPos,
	float lightInvRadiusSq,
	float3 lightColor
)
{
	float3 dif = lightPos - worldPos;
	float att = GetDistanceAttenuation(dif, lightInvRadiusSq);

	return lightColor * att / (4.0f * F_PI);
}

float3 EvaluatePointLightReflection
(
	float3 baseColor,
	float metallic,
	float roughness,
	float3 N,
	float3 V,
	float3 worldPos,
	float3 lightPos,
	float invRadiusSq,
	float3 color,
	float intensity
)
{
	float3 L = normalize(lightPos - worldPos);
	float3 H = normalize(V + L);
	float VH = saturate(dot(V, H));
	float NH = saturate(dot(N, H));
	float NV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float3 brdf = ComputeBRDF
	(
		baseColor,
		metallic,
		roughness,
		VH,
		NH,
		NV,
		NL 
	);
	float3 light = EvaluatePointLight(N, worldPos, lightPos, invRadiusSq, color) * intensity;
	return brdf * light;
}

float GetAngleAttenuation
(
	float3 normalizedLightVector,
	float3 lightDir,
	float lightAngleScale,
	float lightAngleOffset
)
{
	float cd = dot(lightDir, normalizedLightVector);
	float attenuation = saturate(cd * lightAngleScale + lightAngleOffset);
	attenuation *= attenuation;
	return attenuation;
}

float3 EvaluateSpotLight
(
	float3 N,
	float3 worldPos,
	float3 lightPos,
	float lightInvRadiusSq,
	float3 lightForward,
	float3 lightColor,
	float lightAngleScale,
	float lightAngleOffset
)
{
	float3 unnormalizedLightVector = lightPos - worldPos;
	float sqrDist = dot(unnormalizedLightVector, unnormalizedLightVector);
	float att = 1.0f / max(sqrDist, MIN_DIST * MIN_DIST);
	float3 L = normalize(unnormalizedLightVector);
	att *= GetAngleAttenuation(L, -lightForward, lightAngleScale, lightAngleOffset);
	return lightColor * att / F_PI;
}

float3 EvaluateSpotLightKaris
(
	float3 N,
	float3 worldPos,
	float3 lightPos,
	float lightInvRadiusSq,
	float3 lightForward,
	float3 lightColor,
	float lightAngleScale,
	float lightAngleOffset
)
{
	float3 unnormalizedLightVector = lightPos - worldPos;
	float sqrDist = dot(unnormalizedLightVector, unnormalizedLightVector);
	float att = 1.0f / max(sqrDist, MIN_DIST * MIN_DIST);
	float3 L = normalize(unnormalizedLightVector);
	att *= SmoothDistanceAttenuation(sqrDist, lightInvRadiusSq);
	att /= (sqrDist + 1.0f);
	att *= GetAngleAttenuation(L, -lightForward, lightAngleScale, lightAngleOffset);
	return lightColor * att / F_PI;
}

float3 EvaluateSpotLightLagarde
(
	float3 N,
	float3 worldPos,
	float3 lightPos,
	float lightInvRadiusSq,
	float3 lightForward,
	float3 lightColor,
	float lightAngleScale,
	float lightAngleOffset
)
{
	float3 unnormalizedLightVector = lightPos - worldPos;
	float sqrDist = dot(unnormalizedLightVector, unnormalizedLightVector);
	float att = 1.0f / max(sqrDist, MIN_DIST * MIN_DIST);
	float3 L = normalize(unnormalizedLightVector);
	att *= GetDistanceAttenuation(unnormalizedLightVector, lightInvRadiusSq);
	att *= GetAngleAttenuation(L, -lightForward, lightAngleScale, lightAngleOffset);
	return lightColor * att / F_PI;
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

float3 EvaluateSpotLightReflection
(
	float3 baseColor,
	float metallic,
	float roughness,
	float3 N,
	float3 V,
	float3 worldPos,
	float3 lightPos,
	float invSqrRadius,
	float3 forward,
	float3 color,
	float angleScale,
	float angleOffset,
	float intensity,
	Texture2D shadowMap,
	float2 shadowMapSize,
	float3 shadowCoord
)
{
	float3 L = normalize(lightPos - worldPos);
	float3 H = normalize(V + L);
	float VH = saturate(dot(V, H));
	float NH = saturate(dot(N, H));
	float NV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float3 brdf = ComputeBRDF
	(
		baseColor,
		metallic,
		roughness,
		VH,
		NH,
		NV,
		NL 
	);

	//TODO: not branching by type
	float3 light = EvaluateSpotLight(N, worldPos, lightPos, invSqrRadius, forward, color, angleScale, angleOffset) * intensity;
	float transitionScale = SHADOW_SOFT_TRANSITION_SCALE * lerp(PROJECTION_DEPTH_BIAS, 1, NL);
	float shadow = GetShadowMultiplier(shadowMap, shadowMapSize, shadowCoord, transitionScale);
	return brdf * light * shadow;
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

	float transitionScale = SHADOW_SOFT_TRANSITION_SCALE * lerp(PROJECTION_DEPTH_BIAS, 1, dirLightNL);
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, DirLightShadowMapSize, input.DirLightShadowCoord, transitionScale );
	float3 dirLightReflection = dirLightBRDF * DirLightColor * DirLightIntensity * dirLightShadowMult;

	// 4 point light
	float3 pointLight1Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		PointLight1Position,
		PointLight1InvSqrRadius,
		PointLight1Color,
		PointLight1Intensity
	);

	float3 pointLight2Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		PointLight2Position,
		PointLight2InvSqrRadius,
		PointLight2Color,
		PointLight2Intensity
	);

	float3 pointLight3Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		PointLight3Position,
		PointLight3InvSqrRadius,
		PointLight3Color,
		PointLight3Intensity
	);

	float3 pointLight4Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		PointLight4Position,
		PointLight4InvSqrRadius,
		PointLight4Color,
		PointLight4Intensity
	);

	// 3 spot light
	float3 spotLight1Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		SpotLight1Position,
		SpotLight1InvSqrRadius,
		SpotLight1Forward,
		SpotLight1Color,
		SpotLight1AngleScale,
		SpotLight1AngleOffset,
		SpotLight1Intensity,
		SpotLight1ShadowMap,
		SpotLight1ShadowMapSize,
		input.SpotLight1ShadowCoord
	);

	float3 spotLight2Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		SpotLight2Position,
		SpotLight2InvSqrRadius,
		SpotLight2Forward,
		SpotLight2Color,
		SpotLight2AngleScale,
		SpotLight2AngleOffset,
		SpotLight2Intensity,
		SpotLight2ShadowMap,
		SpotLight2ShadowMapSize,
		input.SpotLight2ShadowCoord
	);

	float3 spotLight3Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		SpotLight3Position,
		SpotLight3InvSqrRadius,
		SpotLight3Forward,
		SpotLight3Color,
		SpotLight3AngleScale,
		SpotLight3AngleOffset,
		SpotLight3Intensity,
		SpotLight3ShadowMap,
		SpotLight3ShadowMapSize,
		input.SpotLight3ShadowCoord
	);

	output.Color.rgb =
		dirLightReflection
		+ pointLight1Reflection
		+ pointLight2Reflection
		+ pointLight3Reflection
		+ pointLight4Reflection
		+ spotLight1Reflection
		+ spotLight2Reflection
		+ spotLight3Reflection;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;
	return output;
}

