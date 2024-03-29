static const float FLOAT16F_SCALE = 4096.0f * 32.0f; // referred UE // TODO: share it with SSAO_PS.hlsl
static const float THRESHOLD_INVERSE = 0.5f; // 0.5f is from half resolution. refered UE.

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSAOSetup : register(b0)
{
	int Width;
	int Height;
	float Near;
	float Far;
}

Texture2D DepthMap : register(t0);
Texture2D NormalMap : register(t1);
SamplerState PointClampSmp : register(s0);

// TODO: share it with SSAO_PS.hlsl
float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) - Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

// 0: not similar .. 1:very similar
float ComputeDepthSimilarity(float depthA, float depthB, float tweakScale)
{
	return saturate(1 - abs(depthA - depthB) * tweakScale);
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 rcpExtent = rcp(float2(Width, Height));

	float2 uv[4];
	uv[0] = input.TexCoord + float2(-0.5f, -0.5f) * rcpExtent;
	uv[1] = input.TexCoord + float2(0.5f, -0.5f) * rcpExtent;
	uv[2] = input.TexCoord + float2(-0.5f, 0.5f) * rcpExtent;
	uv[3] = input.TexCoord + float2(0.5f, 0.5f) * rcpExtent;

	float4 samples[4];
	for (uint i = 0; i < 4; i++)
	{
		samples[i].xyz = NormalMap.SampleLevel(PointClampSmp, uv[i], 0).xyz;
		float viewZ = ConvertFromDeviceZtoViewZ(DepthMap.SampleLevel(PointClampSmp, uv[i], 0).r);
		// viewZ is negative value.
		samples[i].w = -viewZ;
	}

	float maxZ = max(max(samples[0].w, samples[1].w), max(samples[2].w, samples[3].w));

	float4 avgColor = 0.0001f;
	for (uint j = 0; j < 4; j++)
	{
		avgColor += float4(samples[j].xyz, 1) * ComputeDepthSimilarity(samples[j].w, maxZ, THRESHOLD_INVERSE);
	}
	avgColor.xyz /= avgColor.w;

	return float4(avgColor.xyz, maxZ / FLOAT16F_SCALE);
}