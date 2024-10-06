#include "Particle.hlsli"

#define ROOT_SIGNATURE ""\
"RootConstants(b0, num32BitConstants = 2)" \
", DescriptorTable(CBV(b1))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(SRV(t1))"\
", DescriptorTable(UAV(u1))"\

static const uint NUM_THREAD_X = 64;
static const uint BYTE_OFFSET_INSTANCE_COUNT = 4;
static const float GRAVITY = 9.8f;
static const float PI = 3.14159265358979323f;

cbuffer CbRootConstants : register(b0)
{
	uint NumSpawnPerFrame : packoffset(c0);
	uint InitialLife : packoffset(c0.y);
}

cbuffer CbSimulation : register(b1)
{
	float DeltaTime : packoffset(c0);
	float InitialVelocityScale : packoffset(c0.y);
}

StructuredBuffer<ParticleData> PrevParticlesData : register(t0);
RWStructuredBuffer<ParticleData> CurrParticlesData : register(u0);
ByteAddressBuffer PrevDrawParticlesIndirectArgs : register(t1);
RWByteAddressBuffer CurrDrawParticlesIndirectArgs : register(u1);

groupshared uint gsNumParticleInGroup;
groupshared uint gsGroupParticlesIdxOffset;

float GetRandomNumberLegacy(float2 texCoord, int Seed)
{
    return frac(sin(dot(texCoord.xy, float2(12.9898, 78.233)) + Seed) * 43758.5453);
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(NUM_THREAD_X, 1, 1)]
void main(uint dtID : SV_DispatchThreadID, uint gtID : SV_GroupThreadID)
{
	if (gtID == 0)
	{
		gsNumParticleInGroup = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint prevNumParticles = PrevDrawParticlesIndirectArgs.Load(BYTE_OFFSET_INSTANCE_COUNT);

	ParticleData prevData = PrevParticlesData[dtID];
	ParticleData currData;

	if (dtID < prevNumParticles)
	{
		currData = prevData;
		currData.Life--;
		if (currData.Life <= 0)
		{
			// death
			return;
		}

		// integrate
		currData.Velocity += float3(0, -GRAVITY, 0) * DeltaTime;
		currData.Position += currData.Velocity * DeltaTime;
	}
	else if (dtID < prevNumParticles + NumSpawnPerFrame)
	{
		// spawn
		currData.Position = float3(0, 0, 0);

		float randomVal = GetRandomNumberLegacy(0, dtID);
		float sin, cos;
		sincos(randomVal * 2 * PI, sin, cos);
		currData.Velocity = float3(cos, 2, sin) * randomVal * InitialVelocityScale;

		currData.Life = InitialLife;
	}
	else
	{
		return;
	}

	uint particleIdxInGroup;
	InterlockedAdd(gsNumParticleInGroup, 1, particleIdxInGroup);
	GroupMemoryBarrierWithGroupSync();

	if (particleIdxInGroup == 0)
	{
		CurrDrawParticlesIndirectArgs.InterlockedAdd(BYTE_OFFSET_INSTANCE_COUNT, gsNumParticleInGroup, gsGroupParticlesIdxOffset);
	}
	GroupMemoryBarrierWithGroupSync();

	CurrParticlesData[gsGroupParticlesIdxOffset + particleIdxInGroup] = currData;
}