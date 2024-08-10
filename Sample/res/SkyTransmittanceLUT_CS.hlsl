#define ROOT_SIGNATURE ""\
"DescriptorTable(UAV(u0))"\

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	uint2 pixelPosition = DTid;
	OutResult[pixelPosition] = float4(0, 0, 0, 0);
}