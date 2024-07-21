#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
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
")"\

cbuffer CbSSGI_Denoise : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
}

Texture2D SSGIMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;
static const uint SAMPLE_COUNT = 8;
static const uint STACKOWIAK_SAMPLE_SET_COUNT = 4;

static const int2 STACKOWIAK_SAMPLE_SET_0[8 * 4] =
{
	int2(-1, -1), int2(+1, -1), int2(-1, +1), int2(+1, +1),
	int2(-2, +1), int2(-2, -1), int2(-1, +2), int2(+2, -1),
	int2(+1, -2), int2(+3, -1), int2(+2, +1), int2(-1, -2),
	int2(-2, -3), int2(-1, -3), int2(-2, -2), int2(-1, +3),
	int2(-2, +2), int2(+2, -3), int2(-2, +3), int2(+2, +3),
	int2(+1, -3), int2(-3, -1), int2(-3, -2), int2(-3, +1),
	int2(+1, +2), int2(+1, +3), int2(-4, +1), int2(+1, +4),
	int2(+2, -2), int2(+4, -1), int2(+3, +2), int2(+4, +1),
};

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	uint2 pixelPosition = DTid;
	float4 result = 0;
	float2 rcpDimension = 1.0f / float2(Width, Height);
	float2 bufferUV = (pixelPosition + 0.5f) * rcpDimension;

	// center
	result += SSGIMap.SampleLevel(PointClampSmp, bufferUV, 0);

	// assign 0,1,2,3 to each neighbor 4 pixels
	uint sampleTrackId = (pixelPosition.x & 1) | ((pixelPosition.y & 1) << 1);

	// rest samples
	for (uint i = 1; i < SAMPLE_COUNT; i++)
	{
		int2 sampleOffset = STACKOWIAK_SAMPLE_SET_0[i * STACKOWIAK_SAMPLE_SET_COUNT + sampleTrackId];
		float2 sampleBufferUV = bufferUV + sampleOffset * rcpDimension;
		result += SSGIMap.SampleLevel(PointClampSmp, sampleBufferUV, 0);
	}

	result /= SAMPLE_COUNT;

	OutResult[pixelPosition] = result;
}