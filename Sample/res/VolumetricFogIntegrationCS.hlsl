// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XY = 8;

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	int3 GridSize : packoffset(c4);
	float Near : packoffset(c4.w);
	float Far : packoffset(c5);
}

Texture3D LightScattering : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture3D<float4> OutResult : register(u0);

// TODO: same code for VolumetricFogScatteringCS.hlsl
float ConvertViewZtoDeviceZ(float viewZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ;
}

float3 ConverFromNDCToCameraOriginWS(float4 ndcPos, float viewPosZ)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 cameraOriginWorldPos = mul(InvVRotPMatrix, clipPos);
	
	return cameraOriginWorldPos.xyz;
}

float3 ComputeCellCameraOriginWorldPosition(float3 gridCoordinate, float3 cellOffset)
{
	float2 uv = (gridCoordinate.xy + cellOffset.xy) / GridSize.xy;
	// TODO: exp slice
	float linearDepth = lerp(Near, Far, (gridCoordinate.z + cellOffset.z) / float(GridSize.z));
	float viewPosZ = -linearDepth;
	float deviceZ = ConvertViewZtoDeviceZ(viewPosZ);
	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos, viewPosZ);
	return cameraOriginWorldPos;
}

[numthreads(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 gridCoordinate = DTid;

	float3 prevSliceCameraOriginWorldPos = ComputeCellCameraOriginWorldPosition(gridCoordinate, float3(0.5, 0.5f, 0.0f));

	float3 accmulateLighting = 0;
	float accmulateTransmittance = 1.0f;

	for (int layerIndex = 0; layerIndex < GridSize.z; layerIndex++)
	{
		uint3 layerCoordinate = uint3(gridCoordinate.xy, layerIndex);
		float3 layerCameraOriginWorldPosition = ComputeCellCameraOriginWorldPosition(layerCoordinate, 0.5f);
		float stepLength = length(layerCameraOriginWorldPosition - prevSliceCameraOriginWorldPos);
		prevSliceCameraOriginWorldPos = layerCameraOriginWorldPosition;

		float4 preExposedScatteringAndExtinction = LightScattering[layerCoordinate];

		float transmittance = exp(-preExposedScatteringAndExtinction.w * stepLength);
		float fadeInLerpValue = 1;

		float3 scatteingIntegratedOverSlice = fadeInLerpValue * (preExposedScatteringAndExtinction.rgb - preExposedScatteringAndExtinction.rgb * transmittance) / max(preExposedScatteringAndExtinction.w, 0.00001f);

		accmulateLighting += scatteingIntegratedOverSlice * accmulateTransmittance;

		accmulateTransmittance *= lerp(1.0f, transmittance, fadeInLerpValue);

		OutResult[layerCoordinate] = float4(accmulateLighting, accmulateTransmittance);
	}
}