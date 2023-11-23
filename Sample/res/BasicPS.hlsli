#include "BRDF.hlsli"

#ifndef MIN_DIST
#define MIN_DIST (0.01)
#endif // MIN_DIST

#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
//#define SINGLE_SAMPLE_SHADOW_MAP

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
	float3 ShadowCoord : TEXCOORD2;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
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
	float DirLightShadowTexelSize : packoffset(c1.w);
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

Texture2D BaseColorMap : register(t0);
SamplerState BaseColorSmp : register(s0);

Texture2D MetallicRoughnessMap : register(t1);
SamplerState MetallicRoughnessSmp : register(s1);

Texture2D NormalMap : register(t2);
SamplerState NormalSmp : register(s2);

Texture2D ShadowMap : register(t3);
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s3);
#else
SamplerState ShadowSmp : register(s3);
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
	att *= GetAngleAttenuation(-L, lightForward, lightAngleScale, lightAngleOffset);
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
	att *= GetAngleAttenuation(-L, lightForward, lightAngleScale, lightAngleOffset);
	return saturate(dot(N, L)) * lightColor * att / F_PI;
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
	att *= GetAngleAttenuation(-L, lightForward, lightAngleScale, lightAngleOffset);
	return lightColor * att / F_PI;
}

float GetDirectionalShadowMultiplier(float3 shadowCoord)
{
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	#ifdef SINGLE_SAMPLE_SHADOW_MAP
	float result = ShadowMap.SampleCmpLevelZero(ShadowSmp, shadowCoord.xy, shadowCoord.z);
	#else // SINGLE_SAMPLE_SHADOW_MAP
	const float Dilation = 2.0f;
	float d1 = Dilation * DirLightShadowTexelSize * 0.125f;
	float d2 = Dilation * DirLightShadowTexelSize * 0.875f;
	float d3 = Dilation * DirLightShadowTexelSize * 0.625;
	float d4 = Dilation * DirLightShadowTexelSize * 0.375;
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
	float d1 = Dilation * DirLightShadowTexelSize * 0.125f;
	float d2 = Dilation * DirLightShadowTexelSize * 0.875f;
	float d3 = Dilation * DirLightShadowTexelSize * 0.625;
	float d4 = Dilation * DirLightShadowTexelSize * 0.375;
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

	return result;
}

float3 EvaluateDirectionalLight
(
	float3 shadowCoord,
	float3 lightColor
)
{
	float shadowMult = GetDirectionalShadowMultiplier(shadowCoord);
	//// TODO: temporary indirect lighting
	//shadowMult = shadowMult * 0.5f + 0.5f;
	return lightColor * shadowMult;
}

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;

	float4 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord);
#ifdef ALPHA_MODE_MASK
	if (baseColor.a < AlphaCutoff)
	{
		discard;
	}
#endif

	baseColor.rgb *= BaseColorFactor;

	float2 metallicRoughness = MetallicRoughnessMap.Sample(MetallicRoughnessSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * MetallicFactor;
	float roughness = metallicRoughness.y * RoughnessFactor;

	float3 Kd = baseColor.rgb * (1.0f - metallic);
	float3 Ks = baseColor.rgb * metallic;

	float3 diffuse = ComputeLambert(Kd);

	float3 N = NormalMap.Sample(NormalSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	N = mul(input.InvTangentBasis, N);
	float3 V = normalize(CameraPosition - input.WorldPos);
	float NV = saturate(dot(N, V));

	// DirectionalLight
	float3 dirLightL = normalize(DirLightForward);
	float3 dirLightH = normalize(V + dirLightL);
	float dirLightNH = saturate(dot(N, dirLightH));
	float dirLightNL = saturate(dot(N, dirLightL));
	float3 dirLightSpecular = ComputeGGXSpecular_MultiplyNdotL(Ks, roughness, dirLightNH, NV, dirLightNL);
	float3 dirLightTerm = EvaluateDirectionalLight(input.ShadowCoord, DirLightColor) * DirLightIntensity;
	float3 dirLightColor= (diffuse * dirLightNL + dirLightSpecular) * dirLightTerm;

	// 4 PointLight
	float3 pointLight1L = normalize(PointLight1Position - input.WorldPos);
	float3 pointLight1H = normalize(V + pointLight1L);
	float pointLight1NH = saturate(dot(N, pointLight1H));
	float pointLight1NL = saturate(dot(N, pointLight1L));
	float3 pointLight1Specular = ComputeGGXSpecular_MultiplyNdotL(Ks, roughness, pointLight1NH, NV, pointLight1NL);
	float3 pointLight1Term = EvaluatePointLight(N, input.WorldPos, PointLight1Position, PointLight1InvSqrRadius, PointLight1Color) * PointLight1Intensity;
	float3 pointLight1Color = (diffuse * pointLight1NL + pointLight1Specular) * pointLight1Term;

	float3 pointLight2L = normalize(PointLight2Position - input.WorldPos);
	float3 pointLight2H = normalize(V + pointLight2L);
	float pointLight2NH = saturate(dot(N, pointLight2H));
	float pointLight2NL = saturate(dot(N, pointLight2L));
	float3 pointLight2Specular = ComputeGGXSpecular_MultiplyNdotL(Ks, roughness, pointLight2NH, NV, pointLight2NL);
	float3 pointLight2Term = EvaluatePointLight(N, input.WorldPos, PointLight2Position, PointLight2InvSqrRadius, PointLight2Color) * PointLight2Intensity;
	float3 pointLight2Color = (diffuse * pointLight2NL + pointLight2Specular) * pointLight2Term;

	float3 pointLight3L = normalize(PointLight3Position - input.WorldPos);
	float3 pointLight3H = normalize(V + pointLight3L);
	float pointLight3NH = saturate(dot(N, pointLight3H));
	float pointLight3NL = saturate(dot(N, pointLight3L));
	float3 pointLight3Specular = ComputeGGXSpecular_MultiplyNdotL(Ks, roughness, pointLight3NH, NV, pointLight3NL);
	float3 pointLight3Term = EvaluatePointLight(N, input.WorldPos, PointLight3Position, PointLight3InvSqrRadius, PointLight3Color) * PointLight3Intensity;
	float3 pointLight3Color = (diffuse * pointLight3NL + pointLight3Specular) * pointLight3Term;

	float3 pointLight4L = normalize(PointLight4Position - input.WorldPos);
	float3 pointLight4H = normalize(V + pointLight4L);
	float pointLight4NH = saturate(dot(N, pointLight4H));
	float pointLight4NL = saturate(dot(N, pointLight4L));
	float3 pointLight4Specular = ComputeGGXSpecular_MultiplyNdotL(Ks, roughness, pointLight4NH, NV, pointLight4NL);
	float3 pointLight4Term = EvaluatePointLight(N, input.WorldPos, PointLight4Position, PointLight4InvSqrRadius, PointLight4Color) * PointLight4Intensity;
	float3 pointLight4Color = (diffuse * pointLight4NL + pointLight4Specular) * pointLight4Term;

	output.Color.rgb = dirLightColor + pointLight1Color + pointLight2Color + pointLight3Color + pointLight4Color;
	output.Color.a = 1.0f;
	return output;
}
