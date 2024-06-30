cbuffer CbDenoiseSSGI : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
}

Texture2D SSGIMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

static const float2 STACKOWIAK_SAMPLE_SET_0[8 * 4] =
{
	float2(-0.5, -0.5), float2(+0.5, -0.5), float2(-0.5, +0.5), float2(+0.5, +0.5),
	float2(-1.5, +0.5), float2(-1.5, -0.5), float2(-0.5, +1.5), float2(+1.5, -0.5),
	float2(+0.5, -1.5), float2(+2.5, -0.5), float2(+1.5, +0.5), float2(-0.5, -1.5),
	float2(-1.5, -2.5), float2(-0.5, -2.5), float2(-1.5, -1.5), float2(-0.5, +2.5),
	float2(-1.5, +1.5), float2(+1.5, -2.5), float2(-1.5, +2.5), float2(+1.5, +2.5),
	float2(+0.5, -2.5), float2(-2.5, -0.5), float2(-2.5, -1.5), float2(-2.5, +0.5),
	float2(+0.5, +1.5), float2(+0.5, +2.5), float2(-3.5, +0.5), float2(+0.5, +3.5),
	float2(+1.5, -1.5), float2(+3.5, -0.5), float2(+2.5, +1.5), float2(+3.5, +0.5),
};

[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	uint2 pixelPosition = DTid;
	float4 result = 0;
	float2 bufferUV = (pixelPosition + 0.5f) / float2(Width, Height);

	// center
	result += SSGIMap.SampleLevel(PointClampSmp, bufferUV, 0);

	// assign 0,1,2,3 to each neighbor 4 pixels
	uint sampleTrackId = (pixelPosition.x & 1) | ((pixelPosition.y & 1) << 1);

	OutResult[pixelPosition] = result;
}