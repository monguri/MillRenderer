cbuffer CbTemporalAA : register(b0)
{
	float4x4 ClipToPrevClip;
}

Texture2D ColorMap : register(t0);
Texture2D HitoryMap : register(t1);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	OutResult[DTid.xy] = float4(1, 0, 0, 1);
}