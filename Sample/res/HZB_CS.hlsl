cbuffer CbHZB : register(b0)
{
	int Width;
	int Height;
}

static const uint THREAD_GROUP_SIZE_X = 8;
static const uint THREAD_GROUP_SIZE_Y = 8;

Texture2D DepthMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float> OutResult : register(u0);

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	OutResult[DTid] = 1.0f;
}