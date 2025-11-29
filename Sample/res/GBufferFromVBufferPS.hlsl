#include "ShadowMap.hlsli"
#include "BRDF.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
" | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
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
", filter = FILTER_ANISOTROPIC"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 16"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\
", StaticSampler"\
"("\
"s2"\
", filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_LESS_EQUAL"\
", borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"

// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;
static const uint NUM_POINT_LIGHTS = 4;
static const uint NUM_SPOT_LIGHTS = 3;

struct CbDrawGBufferDescHeapIndices
{
	//uint CbTransform[MAX_MESH_COUNT];
	//uint CbMesh[MAX_MESH_COUNT];
	//uint SbVertexBuffer[MAX_MESH_COUNT];
	//uint SbIndexBuffer[MAX_MESH_COUNT];
	//uint CbMaterial[MAX_MESH_COUNT];
	//uint BaseColorMap[MAX_MESH_COUNT];
	//uint MetallicRoughnessMap[MAX_MESH_COUNT];
	//uint NormalMap[MAX_MESH_COUNT];
	//uint EmissiveMap[MAX_MESH_COUNT];
	//uint AOMap[MAX_MESH_COUNT];

	//uint CbCamera;
	//uint VBuffer;
	//uint DepthBuffer;
	//uint CbGBufferFromVBuffer;

	//// Sponza用
	//uint CbPointLight[NUM_POINT_LIGHTS];
	//uint CbSpotLight[NUM_SPOT_LIGHTS];
	//uint CbDirLight;
	//uint SpotLightShadowMap[NUM_SPOT_LIGHTS];
	//uint DirLightShadowMap;

	//// IBL用
	//uint CbIBL;
	//uint DFGMap;
	//uint DiffuseLDMap;
	//uint SpecularLDMap;

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[(
		MAX_MESH_COUNT * 10 // CbTransform, CbMesh, SbVertexBuffer, SbIndexBuffer, CbMaterial, BaseColorMap, MetallicRoughnessMap, NormalMap, EmissiveMap, AOMap
		+ 1 // CbCamera
		+ 1 // VBuffer
		+ 1 // DepthBuffer
		+ 1 // CbGBufferFromVBuffer
		+ NUM_POINT_LIGHTS // CbPointLight
		+ NUM_SPOT_LIGHTS // CbSpotLight
		+ 1 // CbDirLight
		+ NUM_SPOT_LIGHTS // SpotLightShadowMap
		+ 1   // DirLightShadowMap
		+ 1   // CbIBL
		+ 1   // DFGMap
		+ 1   // DiffuseLDMap
		+ 1   // SpecularLDMap
	) / 4];
};

static const uint CbTransformBaseIdx = 0;
static const uint CbMeshBaseIdx = CbTransformBaseIdx + MAX_MESH_COUNT;
static const uint SbVertexBufferBaseIdx = CbMeshBaseIdx  + MAX_MESH_COUNT;
static const uint SbIndexBufferBaseIdx = SbVertexBufferBaseIdx + MAX_MESH_COUNT;
static const uint CbMaterialBaseIdx = SbIndexBufferBaseIdx + MAX_MESH_COUNT;
static const uint BaseColorMapBaseIdx = CbMaterialBaseIdx + MAX_MESH_COUNT;
static const uint MetallicRoughnessMapBaseIdx = BaseColorMapBaseIdx + MAX_MESH_COUNT;
static const uint NormalMapBaseIdx = MetallicRoughnessMapBaseIdx + MAX_MESH_COUNT;
static const uint EmissiveMapBaseIdx = NormalMapBaseIdx + MAX_MESH_COUNT;
static const uint AOMapBaseIdx = EmissiveMapBaseIdx + MAX_MESH_COUNT;
static const uint CbCameraIdx = MAX_MESH_COUNT * 10;
static const uint VBufferIdx = CbCameraIdx + 1;
static const uint DepthBufferIdx = VBufferIdx + 1;
static const uint CbGBufferFromVBufferIdx = DepthBufferIdx + 1;
static const uint CbPointLightBaseIdx = CbGBufferFromVBufferIdx + 1;
static const uint CbSpotLightBaseIdx = CbPointLightBaseIdx + NUM_POINT_LIGHTS;
static const uint CbDirLightIdx = CbSpotLightBaseIdx + NUM_SPOT_LIGHTS;
static const uint SpotLightShadowMapBaseIdx = CbDirLightIdx + 1;
static const uint DirLightShadowMapIdx = SpotLightShadowMapBaseIdx + NUM_SPOT_LIGHTS;

ConstantBuffer<CbDrawGBufferDescHeapIndices> CbDescHeapIndices : register(b0);

uint GetDescHeapIndex(uint idx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	//uint ret = CbDescHeapIndices.Indices[idx >> 2][idx & 0b11];
	uint ret = CbDescHeapIndices.Indices[idx / 4][idx % 4];
	return ret;
}

SamplerState PointClampSmp : register(s0);
SamplerState AnisotropicWrapSmp : register(s1);
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s2);
#else
SamplerState ShadowSmp : register(s2);
#endif

// TODO:冗長
// VBの頂点構造体
struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

struct Mesh
{
	float4x4 World;
	uint MeshIdx;
};

struct Transform
{
	float4x4 ViewProj;
	float4x4 WorldToDirLightShadowMap;
	float4x4 WorldToSpotLight1ShadowMap;
	float4x4 WorldToSpotLight2ShadowMap;
	float4x4 WorldToSpotLight3ShadowMap;
};

struct Camera
{
	float3 CameraPosition;
	int bDebugViewMeshletCluster;
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

struct GBufferFromVBuffer
{
	float4x4 ViewMatrix;
	float4x4 InvProjMatrix;
	int Width;
	int Height;
	float Near;
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

// C++側の定義と値の一致が必要
static const float INVALID_VISIBILITY = 0xffffffff;

#if 0
// https://shikihuiku.github.io/post/projection_matrix/
// deviceZ = -Near / viewZ
// Nearは0.1mくらいにするので、viewZを100kmまで対応しても安全な値にした
#ifndef DEVICE_Z_MIN_VALUE
#define DEVICE_Z_MIN_VALUE 1e-7f
#endif //DEVICE_Z_FURTHEST

// TODO: いろんなSSパスで冗長
float ConvertFromDeviceZtoViewZ(float deviceZ, float near)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -near / max(deviceZ, DEVICE_Z_MIN_VALUE);
}

float3 ConverFromNDCToVS(float4 ndcPos, float near, float4x4 invProjMat)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float viewPosZ = ConvertFromDeviceZtoViewZ(deviceZ, near);
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 viewPos = mul(invProjMat, clipPos);
	
	return viewPos.xyz;
}
#endif

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// からコードをとってきた
struct BarycentricDeriv
{
	float3 m_lambda;
	float3 m_ddx;
	float3 m_ddy;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	BarycentricDeriv ret = (BarycentricDeriv)0;

	float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	float ddxSum = dot(ret.m_ddx, float3(1,1,1));
	float ddySum = dot(ret.m_ddy, float3(1,1,1));

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);

	ret.m_lambda.x = interpW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
	ret.m_lambda.y = interpW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
	ret.m_lambda.z = interpW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

	ret.m_ddx *= (2.0f/winSize.x);
	ret.m_ddy *= (2.0f/winSize.y);
	ddxSum    *= (2.0f/winSize.x);
	ddySum    *= (2.0f/winSize.y);

	ret.m_ddy *= -1.0f;
	ddySum    *= -1.0f;

	float interpW_ddx = 1.0f / (interpInvW + ddxSum);
	float interpW_ddy = 1.0f / (interpInvW + ddySum);

	ret.m_ddx = interpW_ddx*(ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
	ret.m_ddy = interpW_ddy*(ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;  

	return ret;
}

// 上記記事のInterpolateWithDeriv()を参考にしている
float3 Baryinterpolate3(BarycentricDeriv deriv, float3 v0, float3 v1, float3 v2)
{
	//TDOO: float3x3にまとめてmul()してもよい
	float3 ret;
	ret.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_lambda);
	ret.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_lambda);
	ret.z = dot(float3(v0.z, v1.z, v2.z), deriv.m_lambda);
	return ret;
}

void BaryInterpolateDeriv2(BarycentricDeriv deriv, float2 v0, float2 v1, float2 v2, out float2 interp, out float2 ddx, out float2 ddy)
{
	interp.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_lambda);
	interp.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_lambda);
	ddx.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_ddx);
	ddx.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_ddx);
	ddy.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_ddy);
	ddy.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_ddy);
}

//TODO: ここからSponzaPS.hlsliのコピペなので共通化が必要
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

[RootSignature(ROOT_SIGNATURE)]
PSOutput main(VSOutput input)
{
	Texture2D<uint2> VBuffer = ResourceDescriptorHeap[GetDescHeapIndex(VBufferIdx)];
	uint2 visibility = VBuffer.Sample(PointClampSmp, input.TexCoord);
	uint triangleIdx = visibility.y;
	// visibilityの初期値はINVALID_VISIBILITY。xとyどちらをチェックしてもいいがとりあえずyでチェック
	if (triangleIdx == INVALID_VISIBILITY)
	{
		discard;
	}

	//TODO:現在VBufferが正しくDescHeapからとれてないのでクラッシュさせないための措置
	uint materialId = visibility.x >> 16;
	uint meshIdx = visibility.x & 0xffff;

	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	ConstantBuffer<GBufferFromVBuffer> CbGBufferFromVBuffer = ResourceDescriptorHeap[GetDescHeapIndex(CbGBufferFromVBufferIdx)];

	StructuredBuffer<uint> SbIndexBuffer = ResourceDescriptorHeap[GetDescHeapIndex(SbIndexBufferBaseIdx + meshIdx)];
	uint index0 = SbIndexBuffer[3 * triangleIdx + 0];
	uint index1 = SbIndexBuffer[3 * triangleIdx + 1];
	uint index2 = SbIndexBuffer[3 * triangleIdx + 2];

	StructuredBuffer<VSInput> SbVertexBuffer = ResourceDescriptorHeap[GetDescHeapIndex(SbVertexBufferBaseIdx + meshIdx)];
	VSInput vertex0 = SbVertexBuffer[index0];
	VSInput vertex1 = SbVertexBuffer[index1];
	VSInput vertex2 = SbVertexBuffer[index2];

	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[GetDescHeapIndex(CbMeshBaseIdx + meshIdx)];
	// TODO: 思うに、Triangleの3点がわかるならddx(uv)、ddy(uv)、すなわちDuvDpx、DuvDpyは求まるのでは？ピクセル座標で3頂点のUVからヤコビ案計算でわかりそうなものだ
	// 方法こそ違えど、CalcFullBaryでやっていることと同じでは？
#if 0
	// View Spaceで計算する

	Texture2D DepthBuffer = ResourceDescriptorHeap[CbDescHeapIndices.DepthBuffer];
	float deviceZ = DepthBuffer.Sample(PointClampSmp, input.TexCoord).r;
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 viewPos = ConverFromNDCToVS(ndcPos, CbGBufferFromVBuffer.Near, CbGBufferFromVBuffer.InvProjMatrix);


	float3 vsPos0 = mul(CbGBufferFromVBuffer.ViewMatrix, mul(CbMesh.World, float4(vertex0.Position, 1.0f))).xyz;
	float3 vsPos1 = mul(CbGBufferFromVBuffer.ViewMatrix, mul(CbMesh.World, float4(vertex1.Position, 1.0f))).xyz;
	float3 vsPos2 = mul(CbGBufferFromVBuffer.ViewMatrix, mul(CbMesh.World, float4(vertex2.Position, 1.0f))).xyz;

	float3 triNormal = normalize(cross(vsPos1 - vsPos0, vsPos2 - vsPos0));

	float3 cameraPos = float3(0, 0, 0);
	float3 rayDirection = normalize(viewPos - cameraPos);
	float3 planeSurfacePoint = viewPos;
	float3 planeNormal = triNormal;
	float hitT;
	// 必ず衝突するはずなので戻り値は無視
	RayIntersectPlane(float3(0, 0, 0), normalize(viewPos - cameraPos), viewPos, triNormal, hitT);
#endif

	ConstantBuffer<Transform> CbTransform = ResourceDescriptorHeap[GetDescHeapIndex(CbTransformBaseIdx + meshIdx)];
	float4 clipPos0 = mul(CbTransform.ViewProj, mul(CbMesh.World, float4(vertex0.Position, 1.0f)));
	float4 clipPos1 = mul(CbTransform.ViewProj, mul(CbMesh.World, float4(vertex1.Position, 1.0f)));
	float4 clipPos2 = mul(CbTransform.ViewProj, mul(CbMesh.World, float4(vertex2.Position, 1.0f)));

	BarycentricDeriv barycentricDeriv = CalcFullBary(clipPos0, clipPos1, clipPos2, screenPos, float2(CbGBufferFromVBuffer.Width, CbGBufferFromVBuffer.Height));
	//TODO: SponzaVS.hlslおよびSponzaPS.hlsliの処理と重複するので共通化が必要
	// SponzaVSOutputとSponzaPSOutputを用意して共通関数をhlsliにまとめよう
	// IBL版も同様

	// 頂点出力の各変数の補間
	float3 localPos = Baryinterpolate3(barycentricDeriv, vertex0.Position, vertex1.Position, vertex2.Position);
	float4 worldPos = mul(CbMesh.World, float4(localPos, 1.0f));

	//TODO: ShadowMapをSample()でサンプルするケースではShadowPosのddx/ddyがシャドウマップのSampleGrad()に必要
	float4 dirLightShadowPos = mul(CbTransform.WorldToDirLightShadowMap, worldPos);
	float3 dirLightShadowCoord = dirLightShadowPos.xyz / dirLightShadowPos.w;

	float4 spotLight1ShadowPos = mul(CbTransform.WorldToSpotLight1ShadowMap, worldPos);
	float3 spotLight1ShadowCoord = spotLight1ShadowPos.xyz / spotLight1ShadowPos.w;

	float4 spotLight2ShadowPos = mul(CbTransform.WorldToSpotLight2ShadowMap, worldPos);
	float3 spotLight2ShadowCoord = spotLight2ShadowPos.xyz / spotLight2ShadowPos.w;

	float4 spotLight3ShadowPos = mul(CbTransform.WorldToSpotLight3ShadowMap, worldPos);
	float3 spotLight3ShadowCoord = spotLight3ShadowPos.xyz / spotLight3ShadowPos.w;

	float3 normal = normalize(Baryinterpolate3(barycentricDeriv, vertex0.Normal, vertex1.Normal, vertex2.Normal));
	float3 tangent = normalize(Baryinterpolate3(barycentricDeriv, vertex0.Tangent, vertex1.Tangent, vertex2.Tangent));
	float3 bitangent = normalize(cross(normal, tangent));
	float3x3 invTangentBasis = transpose(float3x3(tangent, bitangent, normal));

	float2 texCoord, texCoordDdx, texCoordDdy;
	BaryInterpolateDeriv2(barycentricDeriv, vertex0.TexCoord, vertex1.TexCoord, vertex2.TexCoord, texCoord, texCoordDdx, texCoordDdy);

	// GBuffer描画に必要なリソースを取得
	ConstantBuffer<Camera> CbCamera = ResourceDescriptorHeap[GetDescHeapIndex(CbCameraIdx)];
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[GetDescHeapIndex(CbMaterialBaseIdx + meshIdx)];

	ConstantBuffer<DirectionalLight> CbDirectionalLight = ResourceDescriptorHeap[GetDescHeapIndex(CbDirLightIdx)];

	ConstantBuffer<PointLight> CbPointLight1 = ResourceDescriptorHeap[GetDescHeapIndex(CbPointLightBaseIdx + 0)];
	ConstantBuffer<PointLight> CbPointLight2 = ResourceDescriptorHeap[GetDescHeapIndex(CbPointLightBaseIdx + 1)];
	ConstantBuffer<PointLight> CbPointLight3 = ResourceDescriptorHeap[GetDescHeapIndex(CbPointLightBaseIdx + 2)];
	ConstantBuffer<PointLight> CbPointLight4 = ResourceDescriptorHeap[GetDescHeapIndex(CbPointLightBaseIdx + 3)];

	ConstantBuffer<SpotLight> CbSpotLight1 = ResourceDescriptorHeap[GetDescHeapIndex(CbSpotLightBaseIdx + 0)];
	ConstantBuffer<SpotLight> CbSpotLight2 = ResourceDescriptorHeap[GetDescHeapIndex(CbSpotLightBaseIdx + 1)];
	ConstantBuffer<SpotLight> CbSpotLight3 = ResourceDescriptorHeap[GetDescHeapIndex(CbSpotLightBaseIdx + 2)];

	Texture2D BaseColorMap = ResourceDescriptorHeap[GetDescHeapIndex(BaseColorMapBaseIdx + meshIdx)];
	Texture2D MetallicRoughnessMap = ResourceDescriptorHeap[GetDescHeapIndex(MetallicRoughnessMapBaseIdx + meshIdx)];
	Texture2D NormalMap = ResourceDescriptorHeap[GetDescHeapIndex(NormalMapBaseIdx + meshIdx)];
	Texture2D EmissiveMap = ResourceDescriptorHeap[GetDescHeapIndex(EmissiveMapBaseIdx + meshIdx)];
	Texture2D AOMap = ResourceDescriptorHeap[GetDescHeapIndex(AOMapBaseIdx + meshIdx)];
	Texture2D DirLightShadowMap = ResourceDescriptorHeap[GetDescHeapIndex(DirLightShadowMapIdx)];
	Texture2D SpotLight1ShadowMap = ResourceDescriptorHeap[GetDescHeapIndex(SpotLightShadowMapBaseIdx + 0)];
	Texture2D SpotLight2ShadowMap = ResourceDescriptorHeap[GetDescHeapIndex(SpotLightShadowMapBaseIdx + 1)];
	Texture2D SpotLight3ShadowMap = ResourceDescriptorHeap[GetDescHeapIndex(SpotLightShadowMapBaseIdx + 2)];

	PSOutput output = (PSOutput)0;

	//TODO: ここからSponzaPS.hlsliの処理と重複するので共通化が必要
	// SampleGradをしてたり、VSOutput input引数がないなどの違いはある
	float4 baseColor = BaseColorMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy);
	// Maskの分岐は既にVBufferで処理済みなのでここでは不要
	baseColor.rgb *= CbMaterial.BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).bg;
	float metallic = metallicRoughness.x * CbMaterial.MetallicFactor;
	float roughness = metallicRoughness.y * CbMaterial.RoughnessFactor;

	float3 N = NormalMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(invTangentBasis, N);
	float3 V = normalize(CbCamera.CameraPosition - worldPos.xyz);
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
		worldPos.xyz,
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
		worldPos.xyz,
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
		worldPos.xyz,
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
		worldPos.xyz,
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
		worldPos.xyz,
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

	float3 spotLight2Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos.xyz,
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

	float3 spotLight3Reflection = EvaluateSpotLightReflection
	(
		baseColor.rgb,
		metallic,
		roughness,
		N,
		V,
		worldPos.xyz,
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

	float3 emissive = 0;
	if (CbMaterial.bExistEmissiveTex)
	{
		emissive = CbMaterial.EmissiveFactor;
		emissive *= EmissiveMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).rgb;
	}

	float AO = 1;
	if (CbMaterial.bExistAOTex)
	{
		AO = AOMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).r;
	}

	//TODO: MeshletでなくVB/IBを使ってるので無理。VBufferを使う場合は別途実装方法を考える必要がある
	//if (CbCamera.bDebugViewMeshletCluster == 0)
	//{
		output.Color.rgb = lit * AO + emissive;
	//}
	//else
	//{
	//	output.Color.rgb = float3
	//	(
	//		float((input.MeshletID & 1) + 1) * 0.5f, // (MeshletID % 2 + 1) / 2.0
	//		float((input.MeshletID & 3) + 1) * 0.25f, // (MeshletID % 4 + 1) / 4.0
	//		float((input.MeshletID & 7) + 1) * 0.125f // (MeshletID % 8 + 1) / 8.0
	//	);
	//}
	output.Color.rgb = lit * AO + emissive;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;

	output.MetallicRoughness.r = metallic;
	output.MetallicRoughness.g = roughness;
	return output;
}