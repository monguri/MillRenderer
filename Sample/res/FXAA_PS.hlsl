// used D3DSamples Fxaa3_11.hlsl default value.
static const float SUB_PIX = 0.75f;
static const float EDGE_THRESHOLD = 0.166f;
static const float EDGE_THRESHOLD_MIN = 0.0833f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbFXAA : register(b0)
{
	int Width;
	int Height;
	int bEnableFXAA;
}

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float RGBtoLuma(float3 rgb)
{
	return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 rcpExtent = float2(1.0f / Width, 1.0f / Height);

	float3 rgbM = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	float3 rgbNW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, -1) * rcpExtent).rgb;
	float3 rgbSW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, +1) * rcpExtent).rgb;
	float3 rgbNE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, -1) * rcpExtent).rgb;
	float3 rgbSE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, +1) * rcpExtent).rgb;

	float lumaM = RGBtoLuma(rgbM);
	float lumaNW = RGBtoLuma(rgbNW);
	float lumaSW = RGBtoLuma(rgbSW);
	float lumaNE = RGBtoLuma(rgbNE);
	float lumaSE = RGBtoLuma(rgbSE);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaSW), min(lumaNE, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaSW), max(lumaNE, lumaSE)));

	// early return
	if ((lumaMax - lumaMin) < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD))
	{
		return float4(rgbM, 1);
	}

	return float4(rgbM, 1);
}
