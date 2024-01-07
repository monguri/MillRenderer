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
RWTexture2D<float4> OutHalfResResult : register(u1);

static const float HISTORY_ALPHA = 0.638511181f; // referenced UE.

static const uint THREAD_GROUP_SIZE_X = 8;
static const uint THREAD_GROUP_SIZE_Y = 8;
static const uint THREAD_GROUP_TOTAL = THREAD_GROUP_SIZE_X * THREAD_GROUP_SIZE_Y;
// 1 is border for 3x3 sample
static const uint TILE_BORDER_SIZE = 1;
static const uint TILE_WIDTH = THREAD_GROUP_SIZE_X + 2 * TILE_BORDER_SIZE;
static const uint TILE_HEIGHT = (THREAD_GROUP_SIZE_Y + 2 * TILE_BORDER_SIZE);
static const uint NUM_TILE = TILE_WIDTH * TILE_HEIGHT;

groupshared float3 TileYCoCgColors[NUM_TILE];

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

uint GetTileIndex(uint2 GTid, uint2 pixelOffset)
{
	uint2 tilePos = GTid + pixelOffset + TILE_BORDER_SIZE;
	return tilePos.x + tilePos.y * TILE_WIDTH;
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID, uint2 GTid : SV_GroupThreadID, uint GTidx : SV_GroupIndex)
{
	//
	// precache tile colors
	//
	uint2 groupTexelOffset = Gid * uint2(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y) - TILE_BORDER_SIZE;

	for (uint tileIdx = GTidx; tileIdx < NUM_TILE; tileIdx += THREAD_GROUP_TOTAL)
	{
		uint2 texelLocation = groupTexelOffset + uint2(tileIdx % TILE_WIDTH, tileIdx / TILE_WIDTH);
		// self clamp to do clamp sampler
		texelLocation = clamp(texelLocation, uint2(0, 0), uint2(Width - 1, Height - 1));

		float3 rgb = ColorMap[texelLocation].rgb;
		TileYCoCgColors[tileIdx] = RGBToYCoCg(rgb);
	}

	GroupMemoryBarrierWithGroupSync();

	//
	// sample current and history colors
	//
	float2 uv = (DTid + 0.5f) / float2(Width, Height);
	// [0, 1] to [-1, 1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.SampleLevel(PointClampSmp, uv, 0).r;

	float4 clipPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, clipPos);
	float2 prevScreenPos = prevClipPos.xy / prevClipPos.w;
	float2 prevUV = prevScreenPos * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

	uint curTileIdx = GetTileIndex(GTid, uint2(0, 0));
	float3 curColor = TileYCoCgColors[curTileIdx];
	//float3 curColor = ColorMap.Load(float3(DTid, 0)).rgb;
	//curColor = RGBToYCoCg(curColor);

	float3 histColor = HistoryMap.SampleLevel(PointClampSmp, prevUV, 0).rgb;
	histColor = RGBToYCoCg(histColor);

	//
	// clamp history color by neighborhood 3x3 current color minmax
	//
	float2 pixelUVoffset = float2(1.0f / Width, 1.0f / Height);

	float3 neighborMin = curColor;
	float3 neighborMax = curColor;

	for (uint i = 0; i < 9; i++)
	{
		// array of (-1, -1) ... (1, 1) 9 elements
		int2 pixelOffset = int2(i % 3, i / 3) - int2(1, 1);
		uint neighborTileIdx = GetTileIndex(GTid, pixelOffset);
		float3 neighborColor = TileYCoCgColors[neighborTileIdx];

		neighborMin = min(neighborMin, neighborColor);
		neighborMax = max(neighborMax, neighborColor);
	}

	histColor = clamp(histColor, neighborMin, neighborMax);

	//
	// blend current and history color
	//
	if (bEnableTemporalAA)
	{
		float3 finalColor = lerp(histColor, curColor, (1.0f - HISTORY_ALPHA));
		finalColor = YCoCgToRGB(finalColor);
		OutResult[DTid] = float4(finalColor, 1.0f);
	}
	else
	{
		// just copy
		curColor = YCoCgToRGB(curColor);
		OutResult[DTid] = float4(curColor, 1.0f);
	}
}