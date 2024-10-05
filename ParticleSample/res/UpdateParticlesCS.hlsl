#include "Particle.hlsli"

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(SRV(t1))"\
", DescriptorTable(UAV(u1))"\

static const uint NUM_THREAD_X = 64;
static const uint BYTE_OFFSET_INSTANCE_COUNT = 4;
static const float Gravity = 9.8f;

cbuffer CbTime : register(b0)
{
	float DeltaTime : packoffset(c0);
}

StructuredBuffer<ParticleData> PrevParticlesData : register(t0);
RWStructuredBuffer<ParticleData> CurrParticlesData : register(u0);
ByteAddressBuffer PrevDrawParticlesIndirectArgs : register(t1);
RWByteAddressBuffer CurrDrawParticlesIndirectArgs : register(u1);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(NUM_THREAD_X, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	uint prevNumParticles = PrevDrawParticlesIndirectArgs.Load(BYTE_OFFSET_INSTANCE_COUNT);

	uint particleIdx = DTid;
	if (particleIdx >= prevNumParticles)
	{
		return;
	}

	ParticleData prevData = PrevParticlesData[particleIdx];
	ParticleData currData = prevData;

	//currData.Velocity += float3(0, -Gravity, 0) * DeltaTime;
	//currData.Position += currData.Velocity * DeltaTime;

	CurrParticlesData[particleIdx] = currData;
	CurrDrawParticlesIndirectArgs.Store(BYTE_OFFSET_INSTANCE_COUNT, prevNumParticles);
}