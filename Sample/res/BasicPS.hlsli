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

cbuffer CbDirectionalLight : register(b1)
{
	float3 LightColor : packoffset(c0);
	float LightIntensity : packoffset(c0.w);
	float3 LightForward : packoffset(c1);
	float ShadowTexelSize : packoffset(c1.w);
};

// TODO:Use ConstantBuffer<>
cbuffer CbPointLight1 : register(b2)
{
	float3 LightPosition1 : packoffset(c0);
	float LightInvSqrRadius1 : packoffset(c0.w);
	float3 LightColor1 : packoffset(c1);
	float LightIntensity1 : packoffset(c1.w);
};

cbuffer CbPointLight2 : register(b3)
{
	float3 LightPosition2 : packoffset(c0);
	float LightInvSqrRadius2 : packoffset(c0.w);
	float3 LightColor2: packoffset(c1);
	float LightIntensity2: packoffset(c1.w);
};

cbuffer CbPointLight3 : register(b4)
{
	float3 LightPosition3 : packoffset(c0);
	float LightInvSqrRadius3 : packoffset(c0.w);
	float3 LightColor3 : packoffset(c1);
	float LightIntensity3 : packoffset(c1.w);
};

cbuffer CbPointLight4 : register(b5)
{
	float3 LightPosition4 : packoffset(c0);
	float LightInvSqrRadius4 : packoffset(c0.w);
	float3 LightColor4 : packoffset(c1);
	float LightIntensity4 : packoffset(c1.w);
};

cbuffer CbCamera : register(b6)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbMaterial : register(b7)
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float AlphaCutoff;
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

	float3 L = normalize(dif);

	return saturate(dot(N, L)) * lightColor * att / (4.0f * F_PI);
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
	return saturate(dot(N, L)) * lightColor * att / F_PI;
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
	return saturate(dot(N, L)) * lightColor * att / F_PI;
}

float GetDirectionalShadowMultiplier(float3 ShadowCoord)
{
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	#ifdef SINGLE_SAMPLE_SHADOW_MAP
	float result = ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy, ShadowCoord.z);
	#else // SINGLE_SAMPLE_SHADOW_MAP
	const float Dilation = 2.0f;
	float d1 = Dilation * ShadowTexelSize * 0.125f;
	float d2 = Dilation * ShadowTexelSize * 0.875f;
	float d3 = Dilation * ShadowTexelSize * 0.625;
	float d4 = Dilation * ShadowTexelSize * 0.375;
	float result = (2.0f * ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy, ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(-d1, d2), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(d2, d1), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(-d3, d4), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(d4, d3), ShadowCoord.z)
		+ ShadowMap.SampleCmpLevelZero(ShadowSmp, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)) / 10.0f;
	#endif // SINGLE_SAMPLE_SHADOW_MAP
#else // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	#ifdef SINGLE_SAMPLE_SHADOW_MAP
	float shadowVal = ShadowMap.Sample(ShadowSmp, ShadowCoord.xy).x;
	float result = 1.0f;
	if (ShadowCoord.z > shadowVal)
	{
		result = 0.0f;
	}
	#else // SINGLE_SAMPLE_SHADOW_MAP
	const float Dilation = 2.0f;
	float d1 = Dilation * ShadowTexelSize * 0.125f;
	float d2 = Dilation * ShadowTexelSize * 0.875f;
	float d3 = Dilation * ShadowTexelSize * 0.625;
	float d4 = Dilation * ShadowTexelSize * 0.375;
	float result = (2.0f * ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(-d2, d1)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(-d1, d2)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(d2, d1)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(d1, d2)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(-d4, d3)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(-d3, d4)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(d4, d3)).x) ? 0.0f : 1.0f)
		+ ((ShadowCoord.z > ShadowMap.Sample(ShadowSmp, ShadowCoord.xy + float2(d3, d4)).x) ? 0.0f : 1.0f)) / 10.0f;
	#endif // SINGLE_SAMPLE_SHADOW_MAP
#endif // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP

	return result;
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

	float3 N = NormalMap.Sample(NormalSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	N = mul(input.InvTangentBasis, N);
	float3 L = normalize(LightForward);
	float3 V = normalize(CameraPosition - input.WorldPos);
	float3 H = normalize(V + L);

	float NH = saturate(dot(N, H));
	float NV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float VH = saturate(dot(V, H));

	baseColor.rgb *= BaseColorFactor;
	float2 metallicRoughness = MetallicRoughnessMap.Sample(MetallicRoughnessSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * MetallicFactor;
	float roughness = metallicRoughness.y * RoughnessFactor;

	float3 Kd = baseColor.rgb * (1.0f - metallic);
	float3 diffuse = ComputeLambert(Kd);

	float3 Ks = baseColor.rgb * metallic;
	float3 specular = 0.0f;
	if (NV > 0.0f)
	{
		specular = ComputeGGX(Ks, roughness, NH, NV, NL);
	}

	float3 BRDF = (diffuse + specular);

	float shadowMult = GetDirectionalShadowMultiplier(input.ShadowCoord);
	// TODO: temporary indirect lighting
	shadowMult = shadowMult * 0.5f + 0.5f;

	output.Color.rgb = BRDF * NL * LightColor * LightIntensity * shadowMult;
	output.Color.a = 1.0f;
	return output;
}
