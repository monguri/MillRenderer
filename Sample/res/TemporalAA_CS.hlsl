static const uint TEMPORAL_AA_NEIGHBORHOOD_SAMPLES = 5;

cbuffer CbTemporalAA : register(b0)
{
	int Width;
	int Height;
	int bEnableTemporalAA;
	float Padding;
	float4 PlusWeights[(TEMPORAL_AA_NEIGHBORHOOD_SAMPLES + 3) / 4];
}

Texture2D ColorMap : register(t0);
Texture2D HistoryMap : register(t1);
Texture2D VelocityMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const float HISTORY_ALPHA = 0.638511181f; // referenced UE.
static const float LUMA_AA_SCALE = 0.01f; // referenced UE.

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

float HdrWeightY(float Y)
{
	return rcp(Y + 4.0f);
}

float2 WeightedLerpFactors(float weightA, float weightB, float blend)
{
	float blendA = (1.0f - blend) * weightA;
	float blendB = blend * weightB;
	float rcpBlend = rcp(blendA + blendB);
	blendA *= rcpBlend;
	blendB *= rcpBlend;
	return float2(blendA, blendB);
}

uint GetTileIndex(uint2 GTid, uint2 pixelOffset)
{
	uint2 tilePos = GTid + pixelOffset + TILE_BORDER_SIZE;
	return tilePos.x + tilePos.y * TILE_WIDTH;
}

// weight for pixel coordinate delta
float ComputeSampleWeight(float2 PixelDelta)
{
	float x2 = saturate(dot(PixelDelta, PixelDelta));
	// 1 - 1.9 * x^2 + 0.9 * x^4
	return (0.905f * x2 - 1.9f) * x2 + 1.0f;
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
	float2 rcpDimension = 1.0f / float2(Width, Height);
	float2 uv = (DTid + 0.5f) * rcpDimension;
	float2 velocity = VelocityMap.SampleLevel(PointClampSmp, uv, 0).rg;
	float2 prevUV = uv - velocity;

	// when prev UV is off screen, ignore history.
	bool bIgnoreHistory = ((min(prevUV.x, prevUV.y) <= 0.0f) || (max(prevUV.x, prevUV.y) >= 1.0f));

	uint curTileIdx = GetTileIndex(GTid, uint2(0, 0));
	float3 colorCur = TileYCoCgColors[curTileIdx];
	//float3 colorCur = ColorMap.Load(float3(DTid, 0)).rgb;
	//colorCur = RGBToYCoCg(colorCur);

	float4 history = HistoryMap.SampleLevel(PointClampSmp, prevUV, 0);
	float3 colorHist = RGBToYCoCg(history.rgb);

	// Anti-ghost dynamic object.
	// referenced UE.
	bool bDynamic = max(velocity.x, velocity.y) > 0;
	{
		// judge dymamic or not by dilated velocity.
		float2 topVelocity = VelocityMap.SampleLevel(PointClampSmp, uv + float2(0, -1) * rcpDimension, 0).rg;
		float2 leftVelocity = VelocityMap.SampleLevel(PointClampSmp, uv + float2(-1, 0) * rcpDimension, 0).rg;
		float2 rightVelocity = VelocityMap.SampleLevel(PointClampSmp, uv + float2(1, 0) * rcpDimension, 0).rg;
		float2 bottomVelocity = VelocityMap.SampleLevel(PointClampSmp, uv + float2(0, 1) * rcpDimension, 0).rg;

		bool bCurDilatedDymamic = bDynamic 
		|| (max(topVelocity.x, topVelocity.y) > 0)
		|| (max(leftVelocity.x, leftVelocity.y) > 0)
		|| (max(rightVelocity.x, rightVelocity.y) > 0)
		|| (max(bottomVelocity.x, bottomVelocity.y) > 0);

		bool bPrevDilatedDynamic = (history.a > 0);
		bIgnoreHistory = bIgnoreHistory || (!bCurDilatedDymamic && bPrevDilatedDynamic);
	}

	//
	// clamp history color by neighborhood 3x3 current color minmax
	//
	float3 neighborMin = colorCur;
	float3 neighborMax = colorCur;

	float3 colorFiltered = 0;
	float finalWeight = 0;

	for (uint i = 0; i < TEMPORAL_AA_NEIGHBORHOOD_SAMPLES; i++)
	{
		// array of (-1, -1) ... (0, 0) 5 elements
		int2 pixelOffset = int2(i % 3, i / 3) - int2(1, 1);
		uint neighborTileIdx = GetTileIndex(GTid, pixelOffset);
		float3 neighborColor = TileYCoCgColors[neighborTileIdx];

		neighborMin = min(neighborMin, neighborColor);
		neighborMax = max(neighborMax, neighborColor);

		//referred FilterCurrentFrameInputSamples() of UE.
		float sampleSpatialWeight = PlusWeights[i / 4][i % 4]; // Gaussian Kernel
		float sampleHDRWeight = HdrWeightY(neighborColor.x);
		float sampleFinalWeight = sampleSpatialWeight * sampleHDRWeight;
		colorFiltered += neighborColor * sampleFinalWeight;
		finalWeight += sampleFinalWeight;
	}

	colorFiltered /= finalWeight;

	// when prev UV is off screen, ignore history.
	if (bIgnoreHistory)
	{
		colorHist = colorFiltered;
	}

	float lumaHist = colorHist.x;
	colorHist = clamp(colorHist, neighborMin, neighborMax);

	//
	// blend current and history color
	//
	if (bEnableTemporalAA)
	{
		float lumaFiltered = colorFiltered.x;

		float blendFinal = (1.0f - HISTORY_ALPHA);
		blendFinal = max(blendFinal, saturate(LUMA_AA_SCALE * lumaHist / abs(lumaFiltered - lumaHist)));

		float weightFiltered = HdrWeightY(colorFiltered.x);
		float weightHist = HdrWeightY(colorHist.x);
		float2 weights = WeightedLerpFactors(weightHist, weightFiltered, blendFinal);

		float3 finalColor = colorHist * weights.x + colorFiltered * weights.y;
		finalColor = YCoCgToRGB(finalColor);
		OutResult[DTid] = float4(finalColor, bDynamic ? 1 : 0);
	}
	else
	{
		// just copy
		colorCur = YCoCgToRGB(colorCur);
		OutResult[DTid] = float4(colorCur, 0.0f);
	}
}