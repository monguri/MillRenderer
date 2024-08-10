#define ROOT_SIGNATURE ""\
"DescriptorTable(UAV(u0))"\

RWTexture2D<float4> OutResult : register(u0);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(1, 1, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	uint2 pixelPosition = DTid;
	OutResult[pixelPosition] = float4(0, 0, 0, 0);
}