#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\


static const uint NUM_THREAD_X = 64;
static const float Gravity = -9.8f;

cbuffer CbTime : register(b0)
{
	float DeltaTime : packoffset(c0);
}

struct ParticleData
{
	float3 Position;
	float3 Velocity;
};

StructuredBuffer<ParticleData> PrevParticlesData : register(t0);
RWStructuredBuffer<ParticleData> CurrParticlesData : register(u0);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(NUM_THREAD_X, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	uint particleIdx = DTid;

	if (particleIdx >= 10) // TODO: hard coding
	{
		return;
	}

	float3 prevPos = PrevParticlesData[particleIdx].Position;
	float3 prevVel = PrevParticlesData[particleIdx].Velocity;
	float3 currVel = prevVel + Gravity * DeltaTime;
	float3 currPos = prevPos + currVel * DeltaTime;

	CurrParticlesData[particleIdx].Position = currPos;
	CurrParticlesData[particleIdx].Velocity = currVel;
}