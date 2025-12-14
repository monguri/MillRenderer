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
	uint MeshletID : MESHLET_ID;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

struct Camera
{
	float3 CameraPosition;
	uint DebugViewType;
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

struct DirectionalLight
{
	float3 DirLightColor;
	float3 DirLightForward;
	float2 DirLightShadowMapSize; // x is pixel size, y is texel size on UV.
};

struct PointLight
{
	float3 PointLightPosition;
	float PointLightInvSqrRadius;
	float3 PointLightColor;
	float PointLightIntensity;
};

struct SpotLight
{
	float3 SpotLightPosition;
	float SpotLightInvSqrRadius;
	float3 SpotLightColor;
	float SpotLightIntensity;
	float3 SpotLightForward;
	float SpotLightAngleScale;
	float SpotLightAngleOffset;
	int SpotLightType;
	float2 SpotLightShadowMapSize; // x is pixel size, y is texel size on UV.
};

SamplerState AnisotropicWrapSmp : register(s0);

#ifdef USE_DYNAMIC_RESOURCE
struct DescHeapIndices
{
	uint CbCamera;
	uint CbMaterial;
	uint CbDirectionalLight;
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
ConstantBuffer<Camera> CbCamera : register(b0);
ConstantBuffer<Material> CbMaterial : register(b1);

ConstantBuffer<DirectionalLight> CbDirectionalLight : register(b2);

ConstantBuffer<PointLight> CbPointLight1 : register(b3);
ConstantBuffer<PointLight> CbPointLight2 : register(b4);
ConstantBuffer<PointLight> CbPointLight3 : register(b5);
ConstantBuffer<PointLight> CbPointLight4 : register(b6);

ConstantBuffer<SpotLight> CbSpotLight1 : register(b7);
ConstantBuffer<SpotLight> CbSpotLight2 : register(b8);
ConstantBuffer<SpotLight> CbSpotLight3 : register(b9);

Texture2D BaseColorMap : register(t0);
Texture2D MetallicRoughnessMap : register(t1);
Texture2D NormalMap : register(t2);
Texture2D EmissiveMap : register(t3);
Texture2D AOMap : register(t4);

Texture2D DirLightShadowMap : register(t5);
Texture2D SpotLight1ShadowMap : register(t6);
Texture2D SpotLight2ShadowMap : register(t7);
Texture2D SpotLight3ShadowMap : register(t8);
#endif // #ifdef USE_DYNAMIC_RESOURCE

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
	float3 light = EvaluateSpotLight(worldPos, lightPos, invSqrRadius, forward, color, angleScale, angleOffset) * intensity;
	float transitionScale = SPOT_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(SPOT_LIGHT_PROJECTION_DEPTH_BIAS, 1, NL);
	float shadow = GetShadowMultiplier(shadowMap, shadowSmp, shadowMapSize, shadowCoord, transitionScale);
	return brdf * light * shadow;
}

PSOutput main(VSOutput input)
{
#ifdef USE_DYNAMIC_RESOURCE
	ConstantBuffer<Camera> CbCamera = ResourceDescriptorHeap[CbDescHeapIndices.CbCamera];
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[CbDescHeapIndices.CbMaterial];

	ConstantBuffer<DirectionalLight> CbDirectionalLight = ResourceDescriptorHeap[CbDescHeapIndices.CbDirectionalLight];

	ConstantBuffer<PointLight> CbPointLight1 = ResourceDescriptorHeap[CbDescHeapIndices.CbPointLight1];
	ConstantBuffer<PointLight> CbPointLight2 = ResourceDescriptorHeap[CbDescHeapIndices.CbPointLight2];
	ConstantBuffer<PointLight> CbPointLight3 = ResourceDescriptorHeap[CbDescHeapIndices.CbPointLight3];
	ConstantBuffer<PointLight> CbPointLight4 = ResourceDescriptorHeap[CbDescHeapIndices.CbPointLight4];

	ConstantBuffer<SpotLight> CbSpotLight1 = ResourceDescriptorHeap[CbDescHeapIndices.CbSpotLight1];
	ConstantBuffer<SpotLight> CbSpotLight2 = ResourceDescriptorHeap[CbDescHeapIndices.CbSpotLight2];
	ConstantBuffer<SpotLight> CbSpotLight3 = ResourceDescriptorHeap[CbDescHeapIndices.CbSpotLight3];

	Texture2D BaseColorMap = ResourceDescriptorHeap[CbDescHeapIndices.BaseColorMap];
	Texture2D MetallicRoughnessMap = ResourceDescriptorHeap[CbDescHeapIndices.MetallicRoughnessMap];
	Texture2D NormalMap = ResourceDescriptorHeap[CbDescHeapIndices.NormalMap];
	Texture2D EmissiveMap = ResourceDescriptorHeap[CbDescHeapIndices.EmissiveMap];
	Texture2D AOMap = ResourceDescriptorHeap[CbDescHeapIndices.AOMap];
	Texture2D DirLightShadowMap = ResourceDescriptorHeap[CbDescHeapIndices.DirLightShadowMap];
	Texture2D SpotLight1ShadowMap = ResourceDescriptorHeap[CbDescHeapIndices.SpotLight1ShadowMap];
	Texture2D SpotLight2ShadowMap = ResourceDescriptorHeap[CbDescHeapIndices.SpotLight2ShadowMap];
	Texture2D SpotLight3ShadowMap = ResourceDescriptorHeap[CbDescHeapIndices.SpotLight3ShadowMap];
#endif //#ifdef USE_DYNAMIC_RESOURCE

	PSOutput output = (PSOutput)0;

	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
#ifdef ALPHA_MODE_MASK
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
#endif

	baseColor.rgb *= CbMaterial.BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.Sample(AnisotropicWrapSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * CbMaterial.MetallicFactor;
	float roughness = metallicRoughness.y * CbMaterial.RoughnessFactor;

	float3 N = NormalMap.Sample(AnisotropicWrapSmp, input.TexCoord).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(input.InvTangentBasis, N);
	float3 V = normalize(CbCamera.CameraPosition - input.WorldPos);
	float NV = saturate(dot(N, V));

	// directional light
	float3 dirLightL = normalize(-CbDirectionalLight.DirLightForward);
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
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, ShadowSmp, CbDirectionalLight.DirLightShadowMapSize, input.DirLightShadowCoord, transitionScale);
	float3 dirLightReflection = dirLightBRDF * CbDirectionalLight.DirLightColor * dirLightShadowMult;

	// 4 point light
	float3 pointLight1Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		CbPointLight1.PointLightPosition,
		CbPointLight1.PointLightInvSqrRadius,
		CbPointLight1.PointLightColor,
		CbPointLight1.PointLightIntensity
	);

	float3 pointLight2Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		CbPointLight2.PointLightPosition,
		CbPointLight2.PointLightInvSqrRadius,
		CbPointLight2.PointLightColor,
		CbPointLight2.PointLightIntensity
	);

	float3 pointLight3Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		CbPointLight3.PointLightPosition,
		CbPointLight3.PointLightInvSqrRadius,
		CbPointLight3.PointLightColor,
		CbPointLight3.PointLightIntensity
	);

	float3 pointLight4Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		input.WorldPos,
		CbPointLight4.PointLightPosition,
		CbPointLight4.PointLightInvSqrRadius,
		CbPointLight4.PointLightColor,
		CbPointLight4.PointLightIntensity
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
		CbSpotLight1.SpotLightPosition,
		CbSpotLight1.SpotLightInvSqrRadius,
		CbSpotLight1.SpotLightForward,
		CbSpotLight1.SpotLightColor,
		CbSpotLight1.SpotLightAngleScale,
		CbSpotLight1.SpotLightAngleOffset,
		CbSpotLight1.SpotLightIntensity,
		SpotLight1ShadowMap,
		ShadowSmp,
		CbSpotLight1.SpotLightShadowMapSize,
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
		CbSpotLight2.SpotLightPosition,
		CbSpotLight2.SpotLightInvSqrRadius,
		CbSpotLight2.SpotLightForward,
		CbSpotLight2.SpotLightColor,
		CbSpotLight2.SpotLightAngleScale,
		CbSpotLight2.SpotLightAngleOffset,
		CbSpotLight2.SpotLightIntensity,
		SpotLight2ShadowMap,
		ShadowSmp,
		CbSpotLight2.SpotLightShadowMapSize,
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
		CbSpotLight3.SpotLightPosition,
		CbSpotLight3.SpotLightInvSqrRadius,
		CbSpotLight3.SpotLightForward,
		CbSpotLight3.SpotLightColor,
		CbSpotLight3.SpotLightAngleScale,
		CbSpotLight3.SpotLightAngleOffset,
		CbSpotLight3.SpotLightIntensity,
		SpotLight3ShadowMap,
		ShadowSmp,
		CbSpotLight3.SpotLightShadowMapSize,
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
	if (CbMaterial.bExistEmissiveTex)
	{
		emissive = CbMaterial.EmissiveFactor;
		emissive *= EmissiveMap.Sample(AnisotropicWrapSmp, input.TexCoord).rgb;
	}

	float AO = 1;
	if (CbMaterial.bExistAOTex)
	{
		AO = AOMap.Sample(AnisotropicWrapSmp, input.TexCoord).r;
	}

	switch (CbCamera.DebugViewType)
	{
	case DEBUG_VIEW_TYPE_NONE:
	default:
		output.Color.rgb = lit * AO + emissive;
		break;
	case DEBUG_VIEW_TYPE_TRIANGLE_INDEX:
		break;
	case DEBUG_VIEW_TYPE_MESHLET_INDEX:
		output.Color.rgb = float3
		(
			float((input.MeshletID & 1) + 1) * 0.5f, // (MeshletID % 2 + 1) / 2.0
			float((input.MeshletID & 3) + 1) * 0.25f, // (MeshletID % 4 + 1) / 4.0
			float((input.MeshletID & 7) + 1) * 0.125f // (MeshletID % 8 + 1) / 8.0
		);
		break;
	}
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;

	output.MetallicRoughness.r = metallic;
	output.MetallicRoughness.g = roughness;
	return output;
}

