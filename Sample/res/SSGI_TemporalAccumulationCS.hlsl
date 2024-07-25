#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(SRV(t1))"\
", DescriptorTable(SRV(t2))"\
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
Texture2D HistoryMap : register(t1);
Texture2D VelocityMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	//
	// sample current and history colors
	//
	uint2 pixelPosition = DTid;
	float2 rcpDimension = 1.0f / float2(Width, Height);
	float2 uv = (pixelPosition + 0.5f) * rcpDimension;
	float2 velocity = VelocityMap.SampleLevel(PointClampSmp, uv, 0).rg;
	float2 prevUV = uv - velocity;

	float4 history = 0;

	// when prev UV is off screen, ignore history.
	bool bIgnoreHistory = ((min(prevUV.x, prevUV.y) <= 0.0f) || (max(prevUV.x, prevUV.y) >= 1.0f));
	if (!bIgnoreHistory)
	{
		history = HistoryMap.SampleLevel(PointClampSmp, prevUV, 0);
	}

	OutResult[pixelPosition] = SSGIMap[pixelPosition];
}