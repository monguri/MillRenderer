cbuffer CbTemporalAA : register(b0)
{
	float4x4 ClipToPrevClip;
}

Texture2D HitoryBuffer : register(t0);
SamplerState HistorySmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	OutResult[DTid.xy] = float4(1, 0, 0, 1);
}