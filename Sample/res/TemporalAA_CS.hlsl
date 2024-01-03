cbuffer CbTemporalAA : register(b0)
{
	float4x4 ClipToPrevClip;
	int Width;
	int Height;
	int bEnableTemporalAA;
}

Texture2D DepthMap : register(t0);
Texture2D ColorMap : register(t1);
Texture2D HistoryMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const float HISTORY_ALPHA = 0.638511181f; // referenced UE.

float3 RGBToYCoCg(float3 RGB)
{
	float Y = dot(RGB, float3(1, 2, 1));
	float Co = dot(RGB, float3(2, 0, -2));
	float Cg = dot(RGB, float3(-1, 2, -1));

	return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 YCoCg)
{
	float Y = YCoCg.x * 0.25f;
	float Co = YCoCg.y * 0.25f;
	float Cg = YCoCg.z * 0.25f;

	float R = Y + Co - Cg;
	float G = Y + Cg;
	float B = Y - Co - Cg;

	return float3(R, G, B);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) / float2(Width, Height);
	// [0, 1] to [-1, 1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.SampleLevel(PointClampSmp, uv, 0).r;

	float4 clipPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, clipPos);
	float2 prevScreenPos = prevClipPos.xy / prevClipPos.w;
	float2 prevUV = prevScreenPos * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

	float3 curColor = ColorMap.SampleLevel(PointClampSmp, uv, 0).rgb;
	curColor = RGBToYCoCg(curColor);

	// neighborhood 3x3 rgb clamp
	float2 pixelUVoffset = float2(1.0f / Width, 1.0f / Height);

	float3 neighborMin = curColor;
	float3 neighborMax = curColor;

	for (uint i = 0; i < 9; i++)
	{
		// array of (-1, -1) ... (1, 1) 9 elements
		int2 indexOffset = int2(i % 3, i / 3) - int2(1, 1);

		float3 neighborColor = ColorMap.SampleLevel(PointClampSmp, uv + indexOffset * pixelUVoffset, 0).rgb;
		neighborColor = RGBToYCoCg(neighborColor);

		neighborMin = min(neighborMin, neighborColor);
		neighborMax = max(neighborMax, neighborColor);
	}

	float3 histColor = HistoryMap.SampleLevel(PointClampSmp, prevUV, 0).rgb;
	histColor = RGBToYCoCg(histColor);
	histColor = clamp(histColor, neighborMin, neighborMax);

	if (bEnableTemporalAA)
	{
		float3 finalColor = lerp(histColor, curColor, (1.0f - HISTORY_ALPHA));
		finalColor = YCoCgToRGB(finalColor);
		OutResult[DTid.xy] = float4(finalColor, 1.0f);
	}
	else
	{
		// just copy
		curColor = YCoCgToRGB(curColor);
		OutResult[DTid.xy] = float4(curColor, 1.0f);
	}
}