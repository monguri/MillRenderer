#include "Common.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\

// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XY = 8;

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	float4x4 ClipToPrevClip : packoffset(c4);
	int3 GridSize : packoffset(c8);
	float Near : packoffset(c8.w);
	float Far : packoffset(c9);
	float3 FrameJitterOffsetValue : packoffset(c9.y);
	float DirectionalLightScatteringIntensity : packoffset(c10);
	float SpotLightScatteringIntensity : packoffset(c10.y);
}

Texture3D LightScattering : register(t0);

RWTexture3D<float4> OutResult : register(u0);

// TODO: same code for VolumetricFogScatteringCS.hlsl
float ConvertViewZtoVolumetricFogDeviceZ(float viewZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ;
}

float3 ConverFromNDCToCameraOriginWS(float4 ndcPos, float viewPosZ)
{
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
	float deviceZ = ConvertViewZtoVolumetricFogDeviceZ(viewPosZ);
	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos, viewPosZ);
	return cameraOriginWorldPos;
}

[RootSignature(ROOT_SIGNATURE)]
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

		float3 scatteingIntegratedOverSlice = fadeInLerpValue * (preExposedScatteringAndExtinction.rgb - preExposedScatteringAndExtinction.rgb * transmittance) / max(preExposedScatteringAndExtinction.w, SMALL_VALUE);

		accmulateLighting += scatteingIntegratedOverSlice * accmulateTransmittance;

		accmulateTransmittance *= lerp(1.0f, transmittance, fadeInLerpValue);

		OutResult[layerCoordinate] = float4(accmulateLighting, accmulateTransmittance);
	}
}