Texture2D HCB : register(t0);
Texture2D HZB : register(t1);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

[numthreads(4, 4, 16)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	OutResult[DTid] = float4(0, 0, 0, 0);
}