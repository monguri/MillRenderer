//TODO: hlsl内RootSignature定義はどう書けばいいかわからないのでとりあえずやらない
struct Camera
{
	float4x4 ViewProj;
	float3 CameraPosition;
	uint DebugViewType;
	float4x4 ViewMatrix;
	float4x4 InvProjMatrix;
	float4x4 InvViewProjMatrix;
	uint Width;
	uint Height;
	float Near;
	float Padding[1];
	float4x4 ProjMatrix;
	float4x4 ClipToPrevClip;
};

struct MeshVertex
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
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

ConstantBuffer<Camera> CbCamera : register(b0);
ConstantBuffer<Material> CbMaterial : register(b1);

RaytracingAccelerationStructure RtAS : register(t0);
StructuredBuffer<MeshVertex> VB : register(t1);
StructuredBuffer<uint> IB : register(t2);
Texture2D<float4> BaseColorMap : register(t3);
Texture2D<float4> NormalMap : register(t4);
Texture2D<float2> MetallicRoughnessMap : register(t5);
Texture2D<float4> EmissiveMap : register(t6);
RWTexture2D<float4> BaseColorTarget : register(u0);
RWTexture2D<float4> NormalTarget : register(u1);
RWTexture2D<float2> MetallicRoughnessTarget : register(u2);
RWTexture2D<float4> EmissiveTarget : register(u3);
RWTexture2D<uint64_t> VBufferTarget : register(u4);

SamplerState LinearWrapSmp : register(s0);

// https://shikihuiku.github.io/post/projection_matrix/
// deviceZ = -Near / viewZ
// Nearは0.1mくらいにするので、viewZを100kmまで対応しても安全な値にした
#ifndef DEVICE_Z_MIN_VALUE
#define DEVICE_Z_MIN_VALUE 1e-7f
#endif //DEVICE_Z_FURTHEST

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
	float4 worldPos = mul(CbCamera.InvViewProjMatrix, clipPos);
	return worldPos.xyz;
}

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

struct [raypayload] Payload
{
	float3 color : read(caller) : write(closesthit, miss);
	float3 normal : read(caller) : write(closesthit, miss);
	float2 metallicRoughness : read(caller) : write(closesthit, miss);
	float3 emissive : read(caller) : write(closesthit, miss);
	float deviceZ : read(caller) : write(closesthit, miss);
};

[shader("raygeneration")]
void rayGeneration()
{
	// (Width, Height, 1)のレイ本数をそのままスクリーンのピクセルに割り当てる
	uint2 rayIndex = DispatchRaysIndex().xy;
	uint2 screenDim = DispatchRaysDimensions().xy;

	float2 ndcXY = (float2(rayIndex) + 0.5f) / float2(screenDim) * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(ndcXY, 1, 1);
	float3 worldPos = ConverFromNDCToWS(ndcPos);

	RayDesc rayDesc;
	rayDesc.Origin = CbCamera.CameraPosition;

	float3 rayDirection = normalize(worldPos - CbCamera.CameraPosition);
	rayDesc.Direction = rayDirection;

	rayDesc.TMin = 0;
	rayDesc.TMax = 100000;

	Payload payload;
	uint rayFlags = 0;
	uint instanceInclusionsMask = 0xFF;
	uint rayContributionToHitGroupIndex = 0;
	uint multiplierForGeometryContributionToHitGroupIndex = 0;
	uint missShaderIndex = 0;

	TraceRay(RtAS, rayFlags, instanceInclusionsMask, rayContributionToHitGroupIndex, multiplierForGeometryContributionToHitGroupIndex, missShaderIndex, rayDesc, payload);

	BaseColorTarget[rayIndex.xy] = float4(payload.color, 1);
	NormalTarget[rayIndex.xy] = float4((payload.normal + 1) * 0.5f, 1);
	MetallicRoughnessTarget[rayIndex.xy] = payload.metallicRoughness;
	EmissiveTarget[rayIndex.xy] = float4(payload.emissive, 1);
	VBufferTarget[rayIndex.xy] = uint64_t(asuint(payload.deviceZ)) << 32;
}

[shader("miss")]
void miss(inout Payload payload)
{
	// light green
	payload.color = float3(0.4, 0.6, 0.2);
	// normalは適当に上向きにしておく
	payload.normal = float3(0, 0, 1);
	// metallicRoughnessは適当に0,1にしておく
	payload.metallicRoughness = float2(0, 1);
	payload.emissive = 0;
	// deviceZはFarPlane無限大
	payload.deviceZ = 0;
}

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrs)
{
	uint primitiveIndex = PrimitiveIndex();
	uint index0 = IB[primitiveIndex * 3 + 0];
	uint index1 = IB[primitiveIndex * 3 + 1];
	uint index2 = IB[primitiveIndex * 3 + 2];

	float2 uv0 = VB[index0].TexCoord;
	float2 uv1 = VB[index1].TexCoord;
	float2 uv2 = VB[index2].TexCoord;

	// IBL版なので、モデル座標がそのままワールド座標の前提
	float3 posWS0 = VB[index0].Position;
	float3 posWS1 = VB[index1].Position;
	float3 posWS2 = VB[index2].Position;

	float4 posCS0 = mul(CbCamera.ViewProj, float4(posWS0, 1));
	float4 posCS1 = mul(CbCamera.ViewProj, float4(posWS1, 1));
	float4 posCS2 = mul(CbCamera.ViewProj, float4(posWS2, 1));

	// (Width, Height, 1)のレイ本数をそのままスクリーンのピクセルに割り当てる
	uint2 rayIndex = DispatchRaysIndex().xy;
	uint2 screenDim = DispatchRaysDimensions().xy;
	float2 ndcXY = (float2(rayIndex) + 0.5f) / float2(screenDim) * float2(2, -2) + float2(-1, 1);

	BarycentricDeriv barycentricDeriv = CalcFullBary(posCS0, posCS1, posCS2, ndcXY, screenDim);

	float2 uv, ddx, ddy;
	BaryInterpolateDeriv2(barycentricDeriv, uv0, uv1, uv2, uv, ddx, ddy);

	payload.color = BaseColorMap.SampleGrad(LinearWrapSmp, uv, ddx, ddy).rgb;
	payload.color *= CbMaterial.BaseColorFactor;

	float3 normal = NormalMap.SampleGrad(LinearWrapSmp, uv, ddx, ddy).xyz * 2 - 1;
	normal = normalize(normal);

	float2 metallicRoughness = MetallicRoughnessMap.SampleGrad(LinearWrapSmp, uv, ddx, ddy);
	//TODO: IsotropicNDFFiltering()はddx/ddy(normal)を使っておりラスタライザ前提の実装で使えない。GBufferPS.hlsliを見てみよ
	//metallicRoughness.y = IsotropicNDFFiltering(normal, metallicRoughness.y);
	payload.metallicRoughness = MetallicRoughnessMap.SampleGrad(LinearWrapSmp, uv, ddx, ddy);
	payload.metallicRoughness *= float2(CbMaterial.MetallicFactor, CbMaterial.RoughnessFactor);

	float3 normalWS0 = VB[index0].Normal;
	float3 normalWS1 = VB[index1].Normal;
	float3 normalWS2 = VB[index2].Normal;
	float3 normalWS = normalize(Baryinterpolate3(barycentricDeriv, normalWS0, normalWS1, normalWS2));

	float3 tangentWS0 = VB[index0].Tangent;
	float3 tangentWS1 = VB[index1].Tangent;
	float3 tangentWS2 = VB[index2].Tangent;
	float3 tangentWS = normalize(Baryinterpolate3(barycentricDeriv, tangentWS0, tangentWS1, tangentWS2));

	float3 bitangentWS = normalize(cross(normalWS, tangentWS));
	float3x3 invTangentBasis = transpose(float3x3(tangentWS, bitangentWS, normalWS));

	payload.normal = mul(invTangentBasis, normal);

	payload.emissive = 0;
	if (CbMaterial.bExistEmissiveTex)
	{
		payload.emissive = EmissiveMap.SampleGrad(LinearWrapSmp, uv, ddx, ddy).rgb;
		payload.emissive *= CbMaterial.EmissiveFactor;
	}

	// Inverse Z、Infinite Far PlaneだとClipSpaceW = ViewZである。
	float3 invViewZs = float3(
		rcp(posCS0.w),
		rcp(posCS1.w),
		rcp(posCS2.w)
	);

	// 重心座標補間は以下を参考にした
	// https://shikihuiku.wordpress.com/2017/05/23/barycentric-coordinates%E3%81%AE%E8%A8%88%E7%AE%97%E3%81%A8perspective-correction-partial-derivative%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6/
	// Inverse Z、Infinite Far Planeなので全頂点のClipSpaceZはNear固定である。
	float3 ndcPosZs = float3(
		posCS0.z * invViewZs.x,
		posCS0.z * invViewZs.y,
		posCS0.z * invViewZs.z
	);

	payload.deviceZ = dot(ndcPosZs, barycentricDeriv.m_lambda);
}

