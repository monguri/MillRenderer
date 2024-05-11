// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XYZ = 4;

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix;
	float Near;
	float Far;
	int3 GridSize;
}

Texture2D DepthMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture3D<float4> OutResult : register(u0);

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

// TODO: same code for SSR_PS.hlsl
float3 ConverFromNDCToCameraOriginWS(float4 ndcPos)
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
	float4 cameraOriginWorldPos = mul(InvVRotPMatrix, clipPos);
	
	return cameraOriginWorldPos.xyz;
}

float GetSceneDeviceZ(float2 uv)
{
	return DepthMap.SampleLevel(PointClampSmp, uv, 0).r;
}

[numthreads(THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 GridCoordinate = DTid;

	float2 uv = (GridCoordinate.xy + 0.5f) / float2(GridSize.xy);

	float deviceZ = GetSceneDeviceZ(uv);
	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos);
	float3 cameraVector = normalize(cameraOriginWorldPos);

	OutResult[DTid] = float4(1, 0, 0, 0);
}