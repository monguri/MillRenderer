#ifdef DRAW_SPONZA
#include "ShadowMap.hlsli"
#endif

#include "BRDF.hlsli"

#ifdef DRAW_SPONZA
	#ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
		#define ROOT_SIGNATURE ""\
		"RootFlags"\
		"("\
		"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
		" | DENY_HULL_SHADER_ROOT_ACCESS"\
		" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
		" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
		" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
		" | DENY_MESH_SHADER_ROOT_ACCESS"\
		")"\
		", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
		", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
		", StaticSampler"\
		"("\
		"s0"\
		", filter = FILTER_MIN_MAG_MIP_POINT"\
		", addressU = TEXTURE_ADDRESS_CLAMP"\
		", addressV = TEXTURE_ADDRESS_CLAMP"\
		", addressW = TEXTURE_ADDRESS_CLAMP"\
		", maxAnisotropy = 1"\
		", comparisonFunc = COMPARISON_NEVER"\
		", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"\
		", StaticSampler"\
		"("\
		"s1"\
		", filter = FILTER_MIN_MAG_MIP_POINT"\
		", addressU = TEXTURE_ADDRESS_CLAMP"\
		", addressV = TEXTURE_ADDRESS_CLAMP"\
		", addressW = TEXTURE_ADDRESS_CLAMP"\
		", maxAnisotropy = 1"\
		", comparisonFunc = COMPARISON_NEVER"\
		", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
		", visibility = SHADER_VISIBILITY_PIXEL"\
		")"
	#else // #ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
		#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
			#define ROOT_SIGNATURE ""\
			"RootFlags"\
			"("\
			"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
			" | DENY_HULL_SHADER_ROOT_ACCESS"\
			" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
			" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
			" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
			" | DENY_MESH_SHADER_ROOT_ACCESS"\
			")"\
			", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
			", StaticSampler"\
			"("\
			"s0"\
			", filter = FILTER_MIN_MAG_MIP_POINT"\
			", addressU = TEXTURE_ADDRESS_CLAMP"\
			", addressV = TEXTURE_ADDRESS_CLAMP"\
			", addressW = TEXTURE_ADDRESS_CLAMP"\
			", maxAnisotropy = 1"\
			", comparisonFunc = COMPARISON_NEVER"\
			", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
			", visibility = SHADER_VISIBILITY_PIXEL"\
			")"\
			", StaticSampler"\
			"("\
			"s1"\
			", filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT"\
			", addressU = TEXTURE_ADDRESS_CLAMP"\
			", addressV = TEXTURE_ADDRESS_CLAMP"\
			", addressW = TEXTURE_ADDRESS_CLAMP"\
			", maxAnisotropy = 1"\
			", comparisonFunc = COMPARISON_LESS_EQUAL"\
			", borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE"\
			", visibility = SHADER_VISIBILITY_PIXEL"\
			")"
		#else // #ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
			#define ROOT_SIGNATURE ""\
			"RootFlags"\
			"("\
			"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
			" | DENY_HULL_SHADER_ROOT_ACCESS"\
			" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
			" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
			" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
			" | DENY_MESH_SHADER_ROOT_ACCESS"\
			")"\
			", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b3), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b4), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b5), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b6), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b7), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b8), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(CBV(b9), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
			", DescriptorTable(SRV(t8), visibility = SHADER_VISIBILITY_PIXEL)"\
			", StaticSampler"\
			"("\
			"s0"\
			", filter = FILTER_MIN_MAG_MIP_POINT"\
			", addressU = TEXTURE_ADDRESS_CLAMP"\
			", addressV = TEXTURE_ADDRESS_CLAMP"\
			", addressW = TEXTURE_ADDRESS_CLAMP"\
			", maxAnisotropy = 1"\
			", comparisonFunc = COMPARISON_NEVER"\
			", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
			", visibility = SHADER_VISIBILITY_PIXEL"\
			")"\
			", StaticSampler"\
			"("\
			"s1"\
			", filter = FILTER_MIN_MAG_LINEAR_MIP_POINT"\
			", addressU = TEXTURE_ADDRESS_CLAMP"\
			", addressV = TEXTURE_ADDRESS_CLAMP"\
			", addressW = TEXTURE_ADDRESS_CLAMP"\
			", maxAnisotropy = 1"\
			", comparisonFunc = COMPARISON_NEVER"\
			", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
			", visibility = SHADER_VISIBILITY_PIXEL"\
			")"
		#endif // #ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	#endif // #ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
#else // #ifdef DRAW_SPONZA
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t5), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t6), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t7), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\
", StaticSampler"\
"("\
"s1"\
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"
#endif // #ifdef DRAW_SPONZA

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
};

struct Camera
{
	float4x4 ViewProj;
	float3 CameraPosition;
	uint DebugViewType;
	float4x4 View;
	float4x4 InvProj;
	float4x4 InvViewProj;
	uint Width;
	uint Height;
	float Near;
	float Padding[1];
};

#ifdef DRAW_SPONZA
struct ShadowTransform
{
	float4x4 WorldToDirLightShadowMap;
	float4x4 WorldToSpotLight1ShadowMap;
	float4x4 WorldToSpotLight2ShadowMap;
	float4x4 WorldToSpotLight3ShadowMap;
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
#else // #ifdef DRAW_SPONZA
struct IBL
{
	float TextureSize;
	float MipCount;
	float LightIntensity;
};
#endif // #ifdef DRAW_SPONZA

ConstantBuffer<Camera> CbCamera : register(b0);

#ifdef DRAW_SPONZA
ConstantBuffer<ShadowTransform> CbShadowTransform : register(b1);
ConstantBuffer<DirectionalLight> CbDirectionalLight : register(b2);

ConstantBuffer<PointLight> CbPointLight1 : register(b3);
ConstantBuffer<PointLight> CbPointLight2 : register(b4);
ConstantBuffer<PointLight> CbPointLight3 : register(b5);
ConstantBuffer<PointLight> CbPointLight4 : register(b6);

ConstantBuffer<SpotLight> CbSpotLight1 : register(b7);
ConstantBuffer<SpotLight> CbSpotLight2 : register(b8);
ConstantBuffer<SpotLight> CbSpotLight3 : register(b9);
#else // #ifdef DRAW_SPONZA
ConstantBuffer<IBL> CbIBL : register(b1);
#endif // #ifdef DRAW_SPONZA

Texture2D<float> DepthMap : register(t0);
Texture2D<float4> GBufferBaseColor : register(t1);
Texture2D<float4> GBufferNormal : register(t2);
Texture2D<float2> GBufferMetallicRoughness : register(t3);
Texture2D<float4> GBufferEmissive : register(t4);

#ifdef DRAW_SPONZA
Texture2D DirLightShadowMap : register(t5);
Texture2D SpotLight1ShadowMap : register(t6);
Texture2D SpotLight2ShadowMap : register(t7);
Texture2D SpotLight3ShadowMap : register(t8);
#else // #ifdef DRAW_SPONZA
Texture2D<float2> DFGMap : register(t5);
TextureCube<float4> DiffuseLDMap : register(t6);
TextureCube<float4> SpecularLDMap : register(t7);
#endif // #ifdef DRAW_SPONZA

SamplerState PointClampSamp : register(s0);

#ifdef DRAW_SPONZA
	#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	SamplerComparisonState ShadowSmp : register(s1);
	#else
	SamplerState ShadowSmp : register(s1);
	#endif
#else // #ifdef DRAW_SPONZA
SamplerState LinearWrapSmp : register(s1);
#endif // #ifdef DRAW_SPONZA

float ConvertViewZtoDeviceZ(float viewZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -CbCamera.Near / viewZ;
}

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -CbCamera.Near / max(deviceZ, DEVICE_Z_MIN_VALUE);
}

float3 ConverFromNDCToWS(float4 ndcPos)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float viewPosZ = ConvertFromDeviceZtoViewZ(deviceZ);
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 viewPos = mul(CbCamera.InvViewProj, clipPos);
	
	return viewPos.xyz;
}

#ifdef DRAW_SPONZA
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
	float NdotV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float3 brdf = ComputeBRDF
	(
		baseColor,
		metallic,
		roughness,
		VH,
		NH,
		NdotV,
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
	float NdotV = saturate(dot(N, V));
	float NL = saturate(dot(N, L));
	float3 brdf = ComputeBRDF
	(
		baseColor,
		metallic,
		roughness,
		VH,
		NH,
		NdotV,
		NL 
	);

	//TODO: not branching by type
	float3 light = EvaluateSpotLight(worldPos, lightPos, invSqrRadius, forward, color, angleScale, angleOffset) * intensity;
	float transitionScale = SPOT_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(SPOT_LIGHT_PROJECTION_DEPTH_BIAS, 1, NL);
	float shadow = GetShadowMultiplier(shadowMap, shadowSmp, shadowMapSize, shadowCoord, transitionScale);
	return brdf * light * shadow;
}
#else // #ifdef DRAW_SPONZA
float3 GetSpecularDominantDir(float3 N, float3 R, float roughness)
{
	float smoothness = saturate(1.0f - roughness);
	float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(N, R, lerpFactor);
}

float3 EvaluateIBLDiffuse(float3 N)
{
	return DiffuseLDMap.Sample(LinearWrapSmp, N).rgb;
}

// Referenced glTF-Sample-Viewer ibl.glsl
float3 GetIBLRadianceLambertian(float3 N, float3 NdotV, float roughness, float3 diffuseColor, float3 F0, float3 Fr, float2 f_ab)
{
	float3 irradiance = DiffuseLDMap.Sample(LinearWrapSmp, N).rgb;

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera

	float3 k_S = F0 + Fr * pow(1.0f - NdotV, 5.0f);
	float3 FssEss = k_S * f_ab.x + f_ab.y; // <--- GGX / specular light contribution (scale it down if the specularWeight is low)

    // Multiple scattering, from Fdez-Aguera
	float Ems = (1.0f - (f_ab.x + f_ab.y));
	float3 F_avg = (F0 + (1.0f - F0) / 21.0f);
	float3 FmsEms = Ems * FssEss * F_avg / (1.0f - F_avg * Ems);
	float3 k_D = diffuseColor * (1.0f - FssEss + FmsEms); // we use +FmsEms as indicated by the formula in the blog post (might be a typo in the implementation)

	return (FmsEms + k_D) * irradiance;
}

float RoughnessToMipLevel(float linearRoughness, float mipCount)
{
	return (mipCount - 1) * linearRoughness;
}

float3 EvaluateIBLSpecular
(
	float NdotV,
	float3 N,
	float3 R,
	float3 f0,
	float roughness,
	float textureSize,
	float mipCount
)
{
	float a = roughness * roughness;
	float3 dominantR = GetSpecularDominantDir(N, R, a);

    // 関数を再構築.
    // L * D * (f0 * Gvis * (1 - Fc) + Gvis * Fc) * cosTheta / (4 * NdotL * NdotV).
	NdotV = max(NdotV, 0.5f / textureSize);
	float mipLevel = RoughnessToMipLevel(roughness, mipCount);
	float3 preLD = SpecularLDMap.SampleLevel(LinearWrapSmp, dominantR, mipLevel).xyz;

    // 事前積分したDFGをサンプルする.
    // Fc = ( 1 - HdotL )^5
    // PreIntegratedDFG.r = Gvis * (1 - Fc)
    // PreIntegratedDFG.g = Gvis * Fc
	float2 preDFG = DFGMap.SampleLevel(LinearWrapSmp, float2(NdotV, roughness), 0).xy;

    // LD * (f0 * Gvis * (1 - Fc) + Gvis * Fc)
	return preLD * (f0 * preDFG.x + preDFG.y);
}

// Referenced glTF-Sample-Viewer ibl.glsl
float3 GetIBLRadianceGGX(float3 N, float3 R, float3 NdotV, float roughness, float3 F0, float3 Fr, float2 f_ab, float mipCount)
{
	// TODO: float3 dominantR = GetSpecularDominantDir(N, R, a);なし
	float mipLevel = RoughnessToMipLevel(roughness, mipCount);

	// TODO: NdotV = max(NdotV, 0.5f / textureSize);なし
	// glTF-Sample-Viewer is not using GetSpecularDominantDir()
	float3 specularLight = SpecularLDMap.SampleLevel(LinearWrapSmp, R, mipLevel).xyz;
	
    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
	float3 k_S = F0 + Fr * pow(1.0f - NdotV, 5.0f);
	float3 FssEss = k_S * f_ab.x + f_ab.y;

	return specularLight * FssEss;
}
#endif // #ifdef DRAW_SPONZA


[RootSignature(ROOT_SIGNATURE)]
float4 main(VSOutput input) : SV_TARGET
{
	float deviceZ = DepthMap.Sample(PointClampSamp, input.TexCoord);
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 worldPos = ConverFromNDCToWS(ndcPos);

	float3 baseColor = GBufferBaseColor.Sample(PointClampSamp, input.TexCoord).rgb;
	float2 metallicRoughness = GBufferMetallicRoughness.Sample(PointClampSamp, input.TexCoord);
	float metallic = metallicRoughness.x;
	float roughness = metallicRoughness.y;
	float3 N = GBufferNormal.Sample(PointClampSamp, input.TexCoord).xyz * 2.0f - 1.0f;
	float3 emissive = GBufferEmissive.Sample(PointClampSamp, input.TexCoord).rgb;

	float3 V = normalize(CbCamera.CameraPosition - worldPos);
	float NdotV = saturate(dot(N, V));

#ifdef DRAW_SPONZA
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
		NdotV,
		dirLightNL 
	);

	float4 dirLightShadowPos = mul(CbShadowTransform.WorldToDirLightShadowMap, float4(worldPos, 1));
	// dividing by w is not necessary because it is 1 by orthogonal.
	float3 dirLightShadowCoord = dirLightShadowPos.xyz / dirLightShadowPos.w;
	float transitionScale = DIRECTIONAL_LIGHT_SHADOW_SOFT_TRANSITION_SCALE * lerp(DIRECTIONAL_LIGHT_PROJECTION_DEPTH_BIAS, 1, dirLightNL);
	float dirLightShadowMult = GetShadowMultiplier(DirLightShadowMap, ShadowSmp, CbDirectionalLight.DirLightShadowMapSize, dirLightShadowCoord, transitionScale);
	float3 dirLightReflection = dirLightBRDF * CbDirectionalLight.DirLightColor * dirLightShadowMult;

	// 4 point light
	float3 pointLight1Reflection = EvaluatePointLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos,
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
		worldPos,
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
		worldPos,
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
		worldPos,
		CbPointLight4.PointLightPosition,
		CbPointLight4.PointLightInvSqrRadius,
		CbPointLight4.PointLightColor,
		CbPointLight4.PointLightIntensity
	);

	// 3 spot light
	float4 spotLight1ShadowPos = mul(CbShadowTransform.WorldToSpotLight1ShadowMap, float4(worldPos, 1));
	float3 spotLight1ShadowCoord = spotLight1ShadowPos.xyz / spotLight1ShadowPos.w;

	float3 spotLight1Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos,
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
		spotLight1ShadowCoord
	);

	float4 spotLight2ShadowPos = mul(CbShadowTransform.WorldToSpotLight2ShadowMap, float4(worldPos, 1));
	float3 spotLight2ShadowCoord = spotLight2ShadowPos.xyz / spotLight2ShadowPos.w;

	float3 spotLight2Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos,
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
		spotLight2ShadowCoord 
	);

	float4 spotLight3ShadowPos = mul(CbShadowTransform.WorldToSpotLight3ShadowMap, float4(worldPos, 1));
	float3 spotLight3ShadowCoord = spotLight3ShadowPos.xyz / spotLight3ShadowPos.w;

	float3 spotLight3Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos,
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
		spotLight3ShadowCoord
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

	return float4(lit + emissive, 1.0f);
#else // #ifdef DRAW_SPONZA
	float3 R = normalize(reflect(-V, N));

	float3 cDiff = lerp(baseColor, 0.0f, metallic);;
	float3 F0 = ComputeF0(baseColor, metallic);
	float3 Fr = max(1.0f - roughness, F0) - F0;
	
	float2 f_ab = DFGMap.SampleLevel(LinearWrapSmp, float2(NdotV, roughness), 0).xy;

	float3 diffuse = GetIBLRadianceLambertian(N, NdotV, roughness, cDiff, F0, Fr, f_ab);
	float3 specular = GetIBLRadianceGGX(N, R, NdotV, roughness, F0, Fr, f_ab, CbIBL.MipCount);
	float3 lit = diffuse + specular;

	return float4(lit * CbIBL.LightIntensity + emissive, 1);
#endif // #ifdef DRAW_SPONZA
}