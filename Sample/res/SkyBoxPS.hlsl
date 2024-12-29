#include "Common.hlsli"

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
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

static const float M_TO_KM = 0.001f;

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

cbuffer CbSkyBox : register(b0)
{
	float4x4 WVP : packoffset(c0);
	float4x4 SkyViewLutReferential : packoffset(c4);
	float3 CameraVector : packoffset(c8);
	float ViewHeight : packoffset(c8.w);
	int SkyViewLutWidth : packoffset(c9);
	int SkyViewLutHeight : packoffset(c9.y);
	float BottomRadiusKm : packoffset(c9.z);
};

Texture2D SkyViewLut : register(t0);
SamplerState LinearWrapSampler : register(s0);

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
		uv.y = coord * 0.5f + 0.5f;
	}

	{
		uv.x = atan2(viewDir.x, -viewDir.z) / (2 * F_PI);
	}

	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
	float2 size = float2(SkyViewLutWidth, SkyViewLutHeight);
	uv = FromUnitToSubUvs(uv, size, 1 / size);
}

[RootSignature(ROOT_SIGNATURE)]
PSOutput main()
{
	PSOutput output;

	const float viewHeight = ViewHeight * M_TO_KM;

	// The referencial used to build the Sky View lut
	float3x3 localReferencial = (float3x3)SkyViewLutReferential;
	// Compute inputs in this referential
	float3 worldPosLocal = float3(0, viewHeight, 0);
	float3 upVectorLocal = float3(0, 1, 0);
	float3 worldDirLocal = mul(localReferencial, CameraVector);
	float viewZenithCosAngle = dot(worldDirLocal, upVectorLocal);

	float2 sol = RayIntersectSphere(worldPosLocal, worldDirLocal, float4(0, 0, 0, BottomRadiusKm));
	const bool intersectGround = any(sol > 0);

	float2 skyViewLutUv;
	skyViewLutParamsToUv(intersectGround, viewZenithCosAngle, worldDirLocal, viewHeight, BottomRadiusKm, skyViewLutUv);

	output.Color.xyz = SkyViewLut.SampleLevel(LinearWrapSampler, skyViewLutUv, 0).xyz;
	output.Color.a = 1;

	// 法線は(0, 0, 0)扱い
	output.Normal.xyz = 0.5f;
	output.Normal.a = 1;

	// 反射を起こさないようにメタリックは0、roughnessは1
	output.MetallicRoughness.r = 0;
	output.MetallicRoughness.g = 1;

	return output;
}