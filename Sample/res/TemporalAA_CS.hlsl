cbuffer CbTemporalAA : register(b0)
{
	int Width;
	int Height;
	float4x4 ClipToPrevClip;
}

Texture2D ColorMap : register(t0);
Texture2D HitoryMap : register(t1);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) / float2(Width, Height);
	// TODO: now just copy
	OutResult[DTid.xy] = ColorMap.SampleLevel(PointClampSmp, uv, 0);
}