#include "Particle.hlsli"

cbuffer CbCamera : register(b0)
{
	float4x4 View : packoffset(c0);
	float4x4 Proj : packoffset(c4);
}

StructuredBuffer<ParticleData> ParticlesData : register(t0);

static const float SPRITE_EXTENT = 0.05f;

float4 main(uint instanceID : SV_InstanceID, uint vertexID : SV_VertexID) : SV_POSITION
{
	float3 particleWPos = ParticlesData[instanceID].Position;
	float3 particleVPos = mul(View, float4(particleWPos, 1)).xyz;

	// billboard
	// 0 is left upper vertex
	// 1 is right upper vertex
	// 2 is left lower vertex
	// 3 is right lower vertex
	float viewLocalPosX = (-0.5f + (vertexID % 2)) * SPRITE_EXTENT;
	float viewLocalPosY = (0.5f - (vertexID / 2)) * SPRITE_EXTENT;
	float3 vertexVPos = particleVPos + float3(viewLocalPosX, viewLocalPosY, 0);
	float4 vertexClipPos = mul(Proj, float4(vertexVPos, 1));

	return vertexClipPos;
}