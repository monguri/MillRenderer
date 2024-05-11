// used D3DSamples FXAA3_11.h FXAA_PC default value.
static const float QUALITY_SUBPIX = 0.75f;
static const float QUALITY_EDGE_THRESHOLD = 0.166f;
static const float QUALITY_EDGE_THRESHOLD_MIN = 0.0833f;
// P0 to P11
static const int FXAA_QUALITY__Ps = 12;
static const float FXAA_QUALITY__PN[FXAA_QUALITY__Ps] = {
	1.0f,
	1.5f,
	2.0f,
	2.0f,
	2.0f,
	2.0f,
	2.0f,
	2.0f,
	2.0f,
	2.0f,
	4.0f,
	8.0f,
};

// used D3DSamples FXAA3_11.h FXAA_PC_CONSOLE default value.
static const float CONSOLE_EDGE_THRESHOLD = 0.125f;
static const float CONSOLE_EDGE_THRESHOLD_MIN = 0.05f;
static const float CONSOLE_EDGE_SHARPNESS = 8.0f;

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbFXAA : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
	int bEnableFXAA : packoffset(c0.z);
	int bEnableFXAAHighQuality : packoffset(c0.w);
}

Texture2D ColorMap : register(t0);
SamplerState LinearClampSmp : register(s0);

float RGBtoLuma(float3 rgb)
{
	return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 posM = input.TexCoord;
	float3 rgbM = ColorMap.Sample(LinearClampSmp, posM).rgb;
	if (!bEnableFXAA)
	{
		return float4(rgbM, 1);
	}

	float lumaM = RGBtoLuma(rgbM);
	float2 rcpExtent = float2(1.0f / Width, 1.0f / Height);

	if (bEnableFXAAHighQuality)
	{
		float3 rgbW = ColorMap.Sample(LinearClampSmp, posM + float2(-1, 0) * rcpExtent).rgb;

		float lumaS = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(0, 1) * rcpExtent).rgb);
		float lumaE = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(1, 0) * rcpExtent).rgb);
		float lumaN = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(0, -1) * rcpExtent).rgb);
		float lumaW = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(-1, 0) * rcpExtent).rgb);

		float lumaMin = min(lumaM, min(min(lumaS, lumaE), min(lumaN, lumaW)));
		float lumaMax = max(lumaM, max(max(lumaS, lumaE), max(lumaN, lumaW)));

		// early return
		if ((lumaMax - lumaMin) < max(QUALITY_EDGE_THRESHOLD_MIN, lumaMax * QUALITY_EDGE_THRESHOLD))
		{
			return float4(rgbM, 1);
		}

		float lumaNW = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(-1, -1) * rcpExtent).rgb);
		float lumaSW = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(-1, +1) * rcpExtent).rgb);
		float lumaNE = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(+1, -1) * rcpExtent).rgb);
		float lumaSE = RGBtoLuma(ColorMap.Sample(LinearClampSmp, posM + float2(+1, +1) * rcpExtent).rgb);

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

		// consider gradient
		float gradientN = lumaN - lumaM;
		float gradientS = lumaS - lumaM;
		bool bPairN = (abs(gradientN) >= abs(gradientS));
		if (bPairN)
		{
			lengthSign = -lengthSign;
		}

		float2 posB = posM;
		float2 offNP;
		offNP.x = !bHorzSpan ? 0.0f : rcpExtent.x;
		offNP.y = bHorzSpan ? 0.0f : rcpExtent.y;

		if (!bHorzSpan)
		{
			posB.x += lengthSign * 0.5f;
		}

		if (bHorzSpan)
		{
			posB.y += lengthSign * 0.5f;
		}

		float lumaNN = lumaN + lumaM;
		float lumaSS = lumaS + lumaM;
		if (!bPairN)
		{
			lumaNN = lumaSS;
		}

		float2 posN = posB - offNP * FXAA_QUALITY__PN[0];
		float2 posP = posB + offNP * FXAA_QUALITY__PN[0];
		float lumaEndN = 0.0f; // surely initialized by first loop.
		float lumaEndP = 0.0f; // surely initialized by first loop.
		bool bDoneN = false;
		bool bDoneP = false;
		float gradient = max(abs(gradientN), abs(gradientS));
		float gradientScaled = gradient / 4.0f; //TODO: why 4.0?

		for (int n = 1; n < FXAA_QUALITY__Ps && (!bDoneN || !bDoneP); n++)
		{
			if (!bDoneN)
			{
				lumaEndN = RGBtoLuma(ColorMap.SampleLevel(LinearClampSmp, posN, 0).rgb);
				lumaEndN -= lumaNN * 0.5f;
			}
			if (!bDoneP)
			{
				lumaEndP = RGBtoLuma(ColorMap.SampleLevel(LinearClampSmp, posP, 0).rgb);
				lumaEndP -= lumaNN * 0.5f;
			}

			bDoneN = (abs(lumaEndN) >= gradientScaled);
			if (!bDoneN)
			{
				posN -= offNP * FXAA_QUALITY__PN[n];
			}
			bDoneP = (abs(lumaEndP) >= gradientScaled);
			if (!bDoneP)
			{
				posP += offNP * FXAA_QUALITY__PN[n];
			}
		}

		float dstN = bHorzSpan ? (posM.x - posN.x) : (posM.y - posN.y);
		float dstP = bHorzSpan ? (posP.x - posM.x) : (posP.y - posM.y);
		float spanLength = dstN + dstP;
		float dstMin = min(dstN, dstP);
		float pixelOffset = -dstMin / spanLength + 0.5f;

		// good span check
		float lumaMM = lumaM - lumaNN * 0.5f;
		bool lumaMLTZero = (lumaMM < 0.0f);
		bool bGoodSpanN = (lumaEndN < 0.0f) != lumaMLTZero;
		bool bGoodSpanP = (lumaEndP < 0.0f) != lumaMLTZero;
		bool bDirecionN = (dstN < dstP);
		bool bGoodSpan = bDirecionN ? bGoodSpanN : bGoodSpanP;

		float pixelOffsetGood = bGoodSpan ? pixelOffset : 0.0f;

		float subPixB = ((lumaNS + lumaWE) * 2 + lumaNWSW + lumaNESE) / 12 - lumaM;
		float subPixC = saturate(abs(subPixB) / (lumaMax - lumaMin)); // if lumaMax is neat to lumaMin, early returned already.
		float subPixD = -2.0 * subPixC + 3.0; //TODO: why this equation.
		float subPixE = subPixC * subPixC;
		float subPixF = subPixD * subPixE;
		float subPixG = subPixF * subPixF;
		float subPixH = subPixG * QUALITY_SUBPIX;
		float pixelOffsetSubpix = max(pixelOffsetGood, subPixH);

		if (bHorzSpan)
		{
			posM.y += pixelOffsetSubpix * lengthSign;
		}
		else
		{
			posM.x += pixelOffsetSubpix * lengthSign;
		}

		return float4(ColorMap.Sample(LinearClampSmp, posM).rgb, 1);
	}
	else
	{
		float3 rgbNW = ColorMap.Sample(LinearClampSmp, posM + float2(-1, -1) * 0.5f * rcpExtent).rgb;
		float3 rgbSW = ColorMap.Sample(LinearClampSmp, posM + float2(-1, +1) * 0.5f * rcpExtent).rgb;
		float3 rgbNE = ColorMap.Sample(LinearClampSmp, posM + float2(+1, -1) * 0.5f * rcpExtent).rgb;
		float3 rgbSE = ColorMap.Sample(LinearClampSmp, posM + float2(+1, +1) * 0.5f * rcpExtent).rgb;

		float lumaNW = RGBtoLuma(rgbNW);
		float lumaSW = RGBtoLuma(rgbSW);
		float lumaNE = RGBtoLuma(rgbNE);
		float lumaSE = RGBtoLuma(rgbSE);

		float lumaMin = min(lumaM, min(min(lumaNW, lumaSW), min(lumaNE, lumaSE)));
		float lumaMax = max(lumaM, max(max(lumaNW, lumaSW), max(lumaNE, lumaSE)));

		// early return
		if ((lumaMax - lumaMin) < max(CONSOLE_EDGE_THRESHOLD_MIN, lumaMax * CONSOLE_EDGE_THRESHOLD))
		{
			return float4(rgbM, 1);
		}

		float2 edgeDir;
		edgeDir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
		edgeDir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
		edgeDir = normalize(edgeDir);

		float3 rgbN1 = ColorMap.Sample(LinearClampSmp, posM - edgeDir * rcpExtent * 0.5f).rgb;
		float3 rgbP1 = ColorMap.Sample(LinearClampSmp, posM + edgeDir * rcpExtent * 0.5f).rgb;
		float3 rgbA = (rgbN1 + rgbP1) * 0.5f;

		float edgeDirAbsMinTimesC = min(abs(edgeDir.x), abs(edgeDir.y)) * CONSOLE_EDGE_SHARPNESS;
		// edgeDirAbsMinTimesC cannot be 0. The case 0 is early returned.
		// TODO:really?
		edgeDir = clamp(edgeDir / edgeDirAbsMinTimesC, -2.0f, 2.0f);

		float3 rgbN2 = ColorMap.Sample(LinearClampSmp, posM - edgeDir * rcpExtent * 2.0f).rgb;
		float3 rgbP2 = ColorMap.Sample(LinearClampSmp, posM + edgeDir * rcpExtent * 2.0f).rgb;
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
