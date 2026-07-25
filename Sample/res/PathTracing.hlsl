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

SamplerState PointClampSmp : register(s0);

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

	float2 uv = uv0 * (1 - attrs.barycentrics.x - attrs.barycentrics.y)
		+ uv1 * attrs.barycentrics.x
		+ uv2 * attrs.barycentrics.y;

	float4 baseColor = BaseColorMap.SampleLevel(PointClampSmp, uv, 0);
	payload.color = baseColor.rgb;
}
