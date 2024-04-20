#define USE_MANUAL_PCF_FOR_SHADOW_MAP
//#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
//#define SINGLE_SAMPLE_SHADOW_MAP

#include "ShadowMap.hlsli"
#include "BRDF.hlsli"

#ifndef MIN_DIST
#define MIN_DIST (0.01)
#endif // MIN_DIST

// referenced UE.
static const float DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE = 6353.17f;
static const float DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS = 0.1f;

//static const float SPOT_LIGHT_SHADOW_SOFT_TRANSITION_SCALE = 60.0f;
//TODO: On UE's spot light, default value is 60, but it creates so wide soft shadow.
static const float SPOT_LIGHT_SHADOW_SOFT_TRANSITION_SCALE = 6353.17f;
static const float SPOT_LIGHT_PROJECTION_DEPTH_BIAS = 0.5f;

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
	float2 MetallicRoughness : SV_TARGET2;
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
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
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
Texture2D EmissiveMap : register(t3);
Texture2D AOMap : register(t4);
SamplerState AnisotropicWrapSmp : register(s0);

Texture2D DirLightShadowMap : register(t5);
Texture2D SpotLight1ShadowMap : register(t6);
Texture2D SpotLight2ShadowMap : register(t7);
Texture2D SpotLight3ShadowMap : register(t8);

#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s1);
#else
SamplerState ShadowSmp : register(s1);
#endif

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
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	SamplerComparisonState shadowSmp,
#else
	SamplerState shadowSmp,
#endif
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
	float transitionScale = SPOT_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(SPOT_LIGHT_PROJECTION_DEPTH_BIAS, 1, NL);
	float shadow = GetShadowMultiplier(shadowMap, shadowSmp, shadowMapSize, shadowCoord, transitionScale);
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

	float transitionScale = DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS, 1, dirLightNL);
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, ShadowSmp, DirLightShadowMapSize, input.DirLightShadowCoord, transitionScale );
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
		ShadowSmp,
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
		ShadowSmp,
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
		ShadowSmp,
		SpotLight3ShadowMapSize,
		input.SpotLight3ShadowCoord
	);

	float3 lit = 
		dirLightReflection
		+ pointLight1Reflection
		+ pointLight2Reflection
		+ pointLight3Reflection
		+ pointLight4Reflection
		+ spotLight1Reflection
		+ spotLight2Reflection
		+ spotLight3Reflection;

	float3 emissive = 0;
	if (bExistEmissiveTex)
	{
		emissive = EmissiveFactor;
		emissive *= EmissiveMap.Sample(AnisotropicWrapSmp, input.TexCoord).rgb;
	}

	float AO = 1;
	if (bExistAOTex)
	{
		AO = AOMap.Sample(AnisotropicWrapSmp, input.TexCoord).r;
	}

	output.Color.rgb = lit * AO + emissive;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;

	output.MetallicRoughness.r = metallic;
	output.MetallicRoughness.g = roughness;
	return output;
}

