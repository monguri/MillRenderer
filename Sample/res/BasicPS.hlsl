#include "BRDF.hlsli"

#ifndef MIN_DIST
#define MIN_DIST (0.01)
#endif // MIN_DIST

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
};

cbuffer CbLight : register(b1)
{
	float3 LightColor : packoffset(c0);
	float LightIntensity : packoffset(c0.w);
	float3 LightForward : packoffset(c1);
};

cbuffer CbCamera : register(b2)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbMaterial : register(b3)
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

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;

	float3 N = NormalMap.Sample(NormalSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	N = mul(input.InvTangentBasis, N);
	float3 L = normalize(LightForward);
	float3 V = normalize(CameraPosition - input.WorldPos);
	float3 H = normalize(V + L);

	float NH = saturate(dot(N, H));
	float NV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float VH = saturate(dot(V, H));

	float3 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord).rgb;
	baseColor *= BaseColorFactor;
	float2 metallicRoughness = MetallicRoughnessMap.Sample(MetallicRoughnessSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * MetallicFactor;
	float roughness = metallicRoughness.y * RoughnessFactor;

	float3 Kd = baseColor * (1.0f - metallic);
	float3 diffuse = ComputeLambert(Kd);

	float3 Ks = baseColor * metallic;
	float3 specular = 0.0f;
	if (NV > 0.0f)
	{
		specular = ComputeGGX(Ks, roughness, NH, NV, NL);
	}

	float3 BRDF = (diffuse + specular);

	output.Color.rgb = BRDF * NL * LightColor * LightIntensity;
	output.Color.a = 1.0f;
	return output;
}