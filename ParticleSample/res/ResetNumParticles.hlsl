#define ROOT_SIGNATURE ""\
"DescriptorTable(UAV(u0))"\

static const uint BYTE_OFFSET_INSTANCE_COUNT = 4;

RWByteAddressBuffer CurrDrawParticlesIndirectArgs : register(u0);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	CurrDrawParticlesIndirectArgs.Store(BYTE_OFFSET_INSTANCE_COUNT, 0);
}