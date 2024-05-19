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

//TODO: common functions with SSAO.
float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;

	if (bEnableVolumetrcFog)
	{
		float deviceZ = DepthMap.SampleLevel(PointClampSmp, input.TexCoord, 0).r;
		float linearZ = -ConvertFromDeviceZtoViewZ(deviceZ);
		float zSlice = (linearZ - Near) / (Far - Near);

		float4 fogInscatteringAndOpacity = VolumetricFogIntegration.SampleLevel(LinearClampSmp, float3(input.TexCoord, zSlice), 0);
		return float4(Color * fogInscatteringAndOpacity.a + fogInscatteringAndOpacity.rgb, 1);
	}
	else
	{
		return float4(Color, 1);
	}
}