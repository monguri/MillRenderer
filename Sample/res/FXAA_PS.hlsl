// used D3DSamples Fxaa3_11.hlsl default value.
static const float EDGE_THRESHOLD = 0.166f;
static const float EDGE_THRESHOLD_MIN = 0.0833f;
static const float EDGE_SHARPNESS = 8.0f;

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
	float3 rgbM = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	if (!bEnableFXAA)
	{
		return float4(rgbM, 1);
	}

	float2 rcpExtent = float2(1.0f / Width, 1.0f / Height);
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

	float2 edgeDir;
	edgeDir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	edgeDir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
	edgeDir = normalize(edgeDir);

	float3 rgbN1 = ColorMap.Sample(PointClampSmp, input.TexCoord - edgeDir * rcpExtent * 0.5f).rgb;
	float3 rgbP1 = ColorMap.Sample(PointClampSmp, input.TexCoord + edgeDir * rcpExtent * 0.5f).rgb;
	float3 rgbA = (rgbN1 + rgbP1) * 0.5f;

	float edgeDirAbsMinTimesC = min(abs(edgeDir.x), abs(edgeDir.y)) * EDGE_SHARPNESS;
	// edgeDirAbsMinTimesC cannot be 0. The case 0 is early returned.
	// TODO:really?
	edgeDir = clamp(edgeDir / edgeDirAbsMinTimesC, -2.0f, 2.0f);

	float3 rgbN2 = ColorMap.Sample(PointClampSmp, input.TexCoord - edgeDir * rcpExtent * 2.0f).rgb;
	float3 rgbP2 = ColorMap.Sample(PointClampSmp, input.TexCoord + edgeDir * rcpExtent * 2.0f).rgb;
	float3 rgbB = (rgbN2 + rgbP2) * 0.25f + rgbA * 0.25f;

	float lumaB = RGBtoLuma(rgbB);
	if ((lumaB < lumaMin) || (lumaB > lumaMax))
	{
		return float4(rgbA, 1);
	}
	else
	{
		return float4(rgbB, 1);
	}
}
