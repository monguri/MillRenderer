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

ConstantBuffer<Camera> CbCamera : register(b0);

RaytracingAccelerationStructure RtAS : register(t0);
StructuredBuffer<MeshVertex> VB : register(t1);
StructuredBuffer<uint> IB : register(t2);
Texture2D<float4> BaseColorMap : register(t3);
RWTexture2D<float4> OutTex : register(u0);

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

	OutTex[rayIndex.xy] = float4(payload.color, 1);
}

[shader("miss")]
void miss(inout Payload payload)
{
	// light green
	payload.color = float3(0.4, 0.6, 0.2);
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

	// IBL晩なので、モデル座標がそのままワールド座標の前提
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

	float4 baseColor = BaseColorMap.SampleGrad(LinearWrapSmp, frac(uv), ddx, ddy);
	payload.color = baseColor.rgb;
}
