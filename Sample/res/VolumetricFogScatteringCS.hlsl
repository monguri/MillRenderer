#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XYZ = 4;

static const float DIRECTIONAL_LIGHT_SCATTERING_INTENSITY = 1.0f; // refered UE
static const float SCATTERING_DISTRIBUTION = 0.2f; // refered UE

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	int3 GridSize : packoffset(c4);
	float Near : packoffset(c4.w);
	float Far : packoffset(c5);
	int bEnableVolumetrcFog : packoffset(c5.y);
}

cbuffer CbDirectionalLight : register(b1)
{
	float3 DirLightColor : packoffset(c0);
	float DirLightIntensity: packoffset(c0.w);
	float3 DirLightForward : packoffset(c1);
	float2 DirLightShadowMapSize : packoffset(c2); // x is pixel size, y is texel size on UV.
};

SamplerState PointClampSmp : register(s0);

RWTexture3D<float4> OutResult : register(u0);

float ConvertViewZtoDeviceZ(float viewZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ;
}

// TODO: same code for SSR_PS.hlsl
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

// Positive g = forward scattering
// Zero g = isotropic
// Negative g = backward scattering
// Follows PBRT convention http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html#PhaseHG
float HenyeyGreensteinPhase(float G, float CosTheta)
{
	// Reference implementation (i.e. not schlick approximation). 
	// See http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
	float Numer = 1.0f - G * G;
	float Denom = 1.0f + G * G + 2.0f * G * CosTheta;
	return Numer / (4.0f * F_PI * Denom * sqrt(Denom));
}

float luminance(float3 linearColor)
{
	return dot(linearColor, float3(0.3f, 0.59f, 0.11f));
}

[numthreads(THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint3 gridCoordinate = DTid;

	float3 cameraOriginWorldPos = ComputeCellCameraOriginWorldPosition(gridCoordinate, 0.5f);
	float3 cameraVector = normalize(cameraOriginWorldPos);

	float3 lightScattering = 0;

	lightScattering += DirLightColor * DirLightIntensity * DIRECTIONAL_LIGHT_SCATTERING_INTENSITY
						* HenyeyGreensteinPhase(SCATTERING_DISTRIBUTION, dot(-DirLightForward, -cameraVector));

	// TODO:
	float4 materialScatteringAndAbsorption = float4(1e-05f, 1e-05f, 1e-05f, 0);
	float extinction = materialScatteringAndAbsorption.w + luminance(materialScatteringAndAbsorption.rgb);

	float4 preExposedScatteringAndExtinction = float4(lightScattering * materialScatteringAndAbsorption.xyz, extinction);

	OutResult[gridCoordinate] = preExposedScatteringAndExtinction;
}