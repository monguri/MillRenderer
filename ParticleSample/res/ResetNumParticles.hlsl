#include "Particle.hlsli"

#define ROOT_SIGNATURE ""\
"RootConstants(b0, num32BitConstants = 1)" \
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(UAV(u1))"\

static const uint BYTE_OFFSET_THREAD_GROUP_COUNT_X = 0;

cbuffer CbRootConstants : register(b0)
{
	uint NumSpawnPerFrame : packoffset(c0);
}

ByteAddressBuffer PrevDrawParticlesIndirectArgs : register(t0);
RWByteAddressBuffer CurrDrawParticlesIndirectArgs : register(u0);
RWByteAddressBuffer DispatchIndirectArgs : register(u1);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint numPrevParticles = PrevDrawParticlesIndirectArgs.Load(BYTE_OFFSET_INSTANCE_COUNT);
	uint threadGroupCountX = (numPrevParticles + NumSpawnPerFrame + NUM_THREAD_X - 1) / NUM_THREAD_X;
	DispatchIndirectArgs.Store(BYTE_OFFSET_THREAD_GROUP_COUNT_X, threadGroupCountX);

	// reset counter
	CurrDrawParticlesIndirectArgs.Store(BYTE_OFFSET_INSTANCE_COUNT, 0);
}