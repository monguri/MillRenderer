struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	int3 GridSize : packoffset(c4);
	float Near : packoffset(c4.w);
	float Far : packoffset(c5);
	int bEnableVolumetrcFog : packoffset(c5.y);
}

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture3D VolumetricFogIntegration : register(t2);
SamplerState PointClampSmp : register(s0);
SamplerState LinearClampSmp : register(s1);

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

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	return float4(Color, 1.0f);
}