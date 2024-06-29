Texture2D SSGIMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	uint2 pixelPosition = DTid;
	OutResult[pixelPosition] = SSGIMap[pixelPosition];
}