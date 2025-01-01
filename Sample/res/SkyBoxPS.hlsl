#include "Common.hlsli"
#include "SkyLutCommon.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

static const float M_TO_KM = 0.001f;
static const float SUN_LIGHT_HALF_APEX_ANGLE_RADIAN = 0.5f * 0.5357f * F_PI / 180.0f; // 0.5357 degree

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 ClipPosition : CLIP_POSITION;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

cbuffer CbSkyBox : register(b0)
{
	float4x4 WVP : packoffset(c0);
	float4x4 InvVRotP : packoffset(c4);
	float4x4 SkyViewLutReferential : packoffset(c8);
	float3 AtmosphereLightDirection : packoffset(c12);
	float ViewHeight : packoffset(c12.w);
	float3 AtmosphereLightLuminance : packoffset(c13);
	int SkyViewLutWidth : packoffset(c13.w);
	int SkyViewLutHeight : packoffset(c14);
	float BottomRadiusKm : packoffset(c14.y);
	float TopRadiusKm : packoffset(c14.z);
};

Texture2D SkyViewLut : register(t0);
Texture2D TransmittanceLut : register(t0);
SamplerState LinearClampSampler : register(s0);

float2 FromUnitToSubUvs(float2 uv, float2 size, float2 invSize)
{
	// UVの[0,1]を[0.5, size + 0.5f] / (size + 1)に分布させる
	return (uv + 0.5f * invSize) * size / (size + 1.0f);
}

void skyViewLutParamsToUv(in bool intersectGround, in float viewZenithCosAngle, in float3 viewDir, in float viewHeight, in float bottomRadius, out float2 uv)
{
	float vHorizon = sqrt(viewHeight * viewHeight - BottomRadiusKm * BottomRadiusKm);
	float cosBeta = vHorizon / viewHeight; // GroundToHorizonCos

	float beta = acos(cosBeta);
	float zenithHorizonAngle = F_PI - beta;
	float viewZenithAngle = acos(viewZenithCosAngle);

	if (intersectGround)
	{
		float coord = (viewZenithAngle - zenithHorizonAngle) / beta;
		coord = sqrt(coord);
		uv.y = coord * 0.5f + 0.5f;
	}
	else
	{
		float coord = viewZenithAngle / zenithHorizonAngle;
		coord = 1.0f - coord;
		coord = sqrt(coord);
		coord = 1.0f - coord;
		uv.y = coord * 0.5f;
	}

	{
		uv.x = atan2(viewDir.x, -viewDir.z) / (2 * F_PI);
	}

	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
	float2 size = float2(SkyViewLutWidth, SkyViewLutHeight);
	uv = FromUnitToSubUvs(uv, size, 1 / size);
}

float3 GetAtmosphereTransmiattance(
	bool intersectGround,
	float viewHeight,
	float viewZenithCosAngle
)
{
	// For each view height entry, transmittance is only stored from zenith to horizon. Earth shadow is not accounted for.
	// It does not contain earth shadow in order to avoid texel linear interpolation artefact when LUT is low resolution.
	// As such, at the most shadowed point of the LUT when close to horizon, pure black with earth shadow is never hit.
	// That is why we analytically compute the virtual planet shadow here.
	if (intersectGround)
	{
		return 0.0f;
	}

	float2 transmittanceLutUv;
	LutTransmittanceParamsToUV(viewHeight, viewZenithCosAngle, BottomRadiusKm, TopRadiusKm, transmittanceLutUv);

	const float3 transmittanceToLight = TransmittanceLut.SampleLevel(LinearClampSampler, transmittanceLutUv, 0).rgb;
	return transmittanceToLight;
}

float3 GetLightDiskLuminance(
	float3 planetCenterToWorldPos, float3 worldDir,
	float3 atmosphereLightDirection,
	float atmosphereLightDiscCosHalfApexAngle,
	float3 atmosphereLightDiscLuminance,
	bool intersectGround,
	float viewHeight,
	float viewZenithCosAngle
)
{
	const float viewDotLight = dot(worldDir, atmosphereLightDirection);
	const float cosHalfApex = atmosphereLightDiscCosHalfApexAngle;
	if (viewDotLight > cosHalfApex)
	{
		const float3 transmittanceToLight = GetAtmosphereTransmiattance(planetCenterToWorldPos, worldDir, intersectGround);

		// Soften out the sun disk to avoid bloom flickering at edge. The soften is applied on the outer part of the disk.
		const float softEdge = saturate(2.0f * (viewDotLight - cosHalfApex) / (1.0f - cosHalfApex));

		return transmittanceToLight * atmosphereLightDiscLuminance * softEdge;
	}

	return 0.0f;
}

[RootSignature(ROOT_SIGNATURE)]
PSOutput main(VSOutput input)
{
	PSOutput output;

	const float viewHeight = ViewHeight * M_TO_KM;

	// The referencial used to build the Sky View lut
	float3x3 localReferencial = (float3x3)SkyViewLutReferential;
	// Compute inputs in this referential
	float3 worldPosLocal = float3(0, viewHeight, 0);
	float3 upVectorLocal = float3(0, 1, 0);

	float3 cameraOriginWorldPos = mul(InvVRotP, input.ClipPosition).xyz;
	float3 cameraVector = -normalize(cameraOriginWorldPos);
	float3 worldDirLocal = mul(localReferencial, -cameraVector);
	float viewZenithCosAngle = dot(worldDirLocal, upVectorLocal);

	float2 sol = RayIntersectSphere(worldPosLocal, worldDirLocal, float4(0, 0, 0, BottomRadiusKm));
	const bool intersectGround = any(sol > 0);

	float2 skyViewLutUv;
	skyViewLutParamsToUv(intersectGround, viewZenithCosAngle, worldDirLocal, viewHeight, BottomRadiusKm, skyViewLutUv);

	float3 skyViewLutColor = SkyViewLut.SampleLevel(LinearClampSampler, skyViewLutUv, 0).xyz;

	float3 atmosphereLightDirLocal = mul(localReferencial, AtmosphereLightDirection);
	float3 lightDiskLuminance = GetLightDiskLuminance(worldPosLocal, worldDirLocal, atmosphereLightDirLocal, SUN_LIGHT_HALF_APEX_ANGLE_RADIAN, AtmosphereLightLuminance, intersectGround );

	output.Color.xyz = skyViewLutColor;
	output.Color.a = 1;

	// 法線は(0, 0, 0)扱い
	output.Normal.xyz = 0.5f;
	output.Normal.a = 1;

	// 反射を起こさないようにメタリックは0、roughnessは1
	output.MetallicRoughness.r = 0;
	output.MetallicRoughness.g = 1;

	return output;
}