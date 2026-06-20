#ifdef DRAW_SPONZA
#include "ShadowMap.hlsli"
#endif

#include "BRDF.hlsli"

#ifdef DRAW_SPONZA
#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
")"
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
	uint Width;
	uint Height;
	float Near;
	float Padding[1];
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

ConstantBuffer<Camera> CbCamera : register(b0);

#ifdef DRAW_SPONZA
ConstantBuffer<DirectionalLight> CbDirectionalLight : register(b1);

ConstantBuffer<PointLight> CbPointLight1 : register(b2);
ConstantBuffer<PointLight> CbPointLight2 : register(b3);
ConstantBuffer<PointLight> CbPointLight3 : register(b4);
ConstantBuffer<PointLight> CbPointLight4 : register(b5);

ConstantBuffer<SpotLight> CbSpotLight1 : register(b6);
ConstantBuffer<SpotLight> CbSpotLight2 : register(b7);
ConstantBuffer<SpotLight> CbSpotLight3 : register(b8);
#else // #ifdef DRAW_SPONZA
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
#endif // #ifdef DRAW_SPONZA

SamplerState PointClampSamp : register(s0);
#ifdef DRAW_SPONZA
	#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
	SamplerComparisonState ShadowSmp : register(s1);
	#else
	SamplerState ShadowSmp : register(s1);
	#endif
#else // #ifdef DRAW_SPONZA
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

float3 ConverFromNDCToVS(float4 ndcPos)
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
	float4 viewPos = mul(CbCamera.InvProj, clipPos);
	
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
#else // #ifdef DRAW_SPONZA
#endif // #ifdef DRAW_SPONZA


[RootSignature(ROOT_SIGNATURE)]
float4 main(VSOutput input) : SV_TARGET
{
	float4 baseColor = GBufferBaseColor.Sample(PointClampSamp, input.TexCoord);
	float2 metallicRoughness = GBufferMetallicRoughness.Sample(PointClampSamp, input.TexCoord);
	float metallic = metallicRoughness.x;
	float roughness = metallicRoughness.y;
	float3 N = GBufferNormal.Sample(PointClampSamp, input.TexCoord).xyz * 2.0f - 1.0f;

	return float4(1.0f, 1.0f, 1.0f, 1.0f);
#ifdef DRAW_SPONZA
#else // #ifdef DRAW_SPONZA
#endif // #ifdef DRAW_SPONZA
}