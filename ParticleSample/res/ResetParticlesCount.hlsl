static const uint BYTE_OFFSET_INSTANCE_COUNT = 4;

RWByteAddressBuffer CurrDrawParticlesIndirectArgs : register(u1);

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	CurrDrawParticlesIndirectArgs.Store(BYTE_OFFSET_INSTANCE_COUNT, 0);
}