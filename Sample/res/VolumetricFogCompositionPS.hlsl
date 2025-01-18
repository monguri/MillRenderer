#include "Common.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\
", StaticSampler"\
"("\
"s1"\
", filter = FILTER_MIN_MAG_MIP_LINEAR"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

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

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture3D VolumetricFogIntegration : register(t2);
SamplerState PointClampSmp : register(s0);
SamplerState LinearClampSmp : register(s1);

//TODO: common functions with SSAO.
float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -Near / max(deviceZ, SMALL_VALUE);
}

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;

	float deviceZ = DepthMap.SampleLevel(PointClampSmp, input.TexCoord, 0).r;
	float linearZ = -ConvertFromDeviceZtoViewZ(deviceZ);
	float zSlice = (linearZ - Near) / (Far - Near);

	float4 fogInscatteringAndOpacity = VolumetricFogIntegration.SampleLevel(LinearClampSmp, float3(input.TexCoord, zSlice), 0);
	return float4(Color * fogInscatteringAndOpacity.a + fogInscatteringAndOpacity.rgb, 1);
}