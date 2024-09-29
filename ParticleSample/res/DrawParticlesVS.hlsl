cbuffer CbCamera : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

struct ParticleData
{
	float3 Position;
};

StructuredBuffer<ParticleData> ParticlesData : register(t0);

float4 main(uint instanceID : SV_InstanceID) : SV_POSITION
{
	float3 pos = ParticlesData[instanceID].Position;
	float4 worldPos = float4(pos.xy, 0.1 * instanceID + pos.z, 1);
	return mul(ViewProj, worldPos);
}