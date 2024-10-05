cbuffer CbCamera : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

struct ParticleData
{
	float3 Position;
	float3 Velocity;
};

StructuredBuffer<ParticleData> ParticlesData : register(t0);

float4 main(uint instanceID : SV_InstanceID) : SV_POSITION
{
	return mul(ViewProj, float4(ParticlesData[instanceID].Position, 1));
}