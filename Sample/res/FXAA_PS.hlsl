// used D3DSamples FXAA3_11.h FXAA_PC_CONSOLE default value.
static const float EDGE_THRESHOLD = 0.125f;
static const float EDGE_THRESHOLD_MIN = 0.05f;
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
	int bEnableFXAAHighQuality;
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

	float lumaM = RGBtoLuma(rgbM);
	float2 rcpExtent = float2(1.0f / Width, 1.0f / Height);

	if (bEnableFXAAHighQuality)
	{
		float3 rgbS = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(0, 1) * rcpExtent).rgb;
		float3 rgbE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(1, 0) * rcpExtent).rgb;
		float3 rgbN = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(0, -1) * rcpExtent).rgb;
		float3 rgbW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, 0) * rcpExtent).rgb;

		float lumaS = RGBtoLuma(rgbS);
		float lumaE = RGBtoLuma(rgbE);
		float lumaN = RGBtoLuma(rgbN);
		float lumaW = RGBtoLuma(rgbW);

		float lumaMin = min(lumaM, min(min(lumaS, lumaE), min(lumaN, lumaW)));
		float lumaMax = max(lumaM, max(max(lumaS, lumaE), max(lumaN, lumaW)));

		// early return
		if ((lumaMax - lumaMin) < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD))
		{
			return float4(rgbM, 1);
		}

		float3 rgbNW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, -1) * rcpExtent).rgb;
		float3 rgbSW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, +1) * rcpExtent).rgb;
		float3 rgbNE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, -1) * rcpExtent).rgb;
		float3 rgbSE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, +1) * rcpExtent).rgb;

		float lumaNW = RGBtoLuma(rgbNW);
		float lumaSW = RGBtoLuma(rgbSW);
		float lumaNE = RGBtoLuma(rgbNE);
		float lumaSE = RGBtoLuma(rgbSE);

		// center sobel filter
		float lumaNS = lumaN + lumaS;
		float lumaWE = lumaW + lumaE;
		float edgeHorzM = (-2.0 * lumaM) + lumaNS;
		float edgeVertM = (-2.0 * lumaM) + lumaWE;

		// north east sobel filter
		float lumaNESE = lumaNE + lumaSE;
		float lumaNWNE = lumaNW + lumaNE;
		float edgeHorzE = (-2.0 * lumaE) + lumaNESE;
		float edgeVertN = (-2.0 * lumaN) + lumaNWNE;

		// south west sobel filter
		float lumaNWSW = lumaNW + lumaSW;
		float lumaSWSE = lumaSW + lumaSE;
		float edgeHorzW = (-2.0 * lumaW) + lumaNWSW;
		float edgeVertS = (-2.0 * lumaS) + lumaSWSE;

		// sobel filter total
		float edgeHorzME = (abs(edgeHorzM) * 2.0) + abs(edgeHorzE);
		float edgeVertMN = (abs(edgeVertM) * 2.0) + abs(edgeVertN);
		float edgeHorz = abs(edgeHorzW) + edgeHorzME;
		float edgeVert = abs(edgeVertS) + edgeVertMN;

		bool bHorzSpan = (edgeHorz > edgeVert);

		if (!bHorzSpan)
		{
			lumaN = lumaW;
			lumaS = lumaE;
		}

		float lengthSign = bHorzSpan ? rcpExtent.y : rcpExtent.x;
		float subPixB = ((lumaNS + lumaWE) * 2 + lumaNWSW + lumaNESE) / 12 - lumaM;

		// consider gradient


		// TODO: impl
		return float4(rgbM, 1);
	}
	else
	{
		float3 rgbNW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, -1) * 0.5f * rcpExtent).rgb;
		float3 rgbSW = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(-1, +1) * 0.5f * rcpExtent).rgb;
		float3 rgbNE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, -1) * 0.5f * rcpExtent).rgb;
		float3 rgbSE = ColorMap.Sample(PointClampSmp, input.TexCoord + float2(+1, +1) * 0.5f * rcpExtent).rgb;

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
}
