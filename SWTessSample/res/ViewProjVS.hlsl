#include "Particle.hlsli"

//#define DYNAMIC_RESOURCES

#ifdef DYNAMIC_RESOURCES
struct CameraData
{
	float4x4 View;
	float4x4 Proj;
};
#else
cbuffer CbCamera : register(b0)
{
	float4x4 View : packoffset(c0);
	float4x4 Proj : packoffset(c4);
}
#endif

#ifndef DYNAMIC_RESOURCES
StructuredBuffer<ParticleData> ParticlesData : register(t0);
#endif

static const float SPRITE_EXTENT = 0.02f;

float4 main(uint instanceID : SV_InstanceID, uint vertexID : SV_VertexID) : SV_POSITION
{
#ifdef DYNAMIC_RESOURCES
	// TODO: 3 and 7 are hardcoded, and m_FrameIndex is not considered.
	ConstantBuffer<CameraData> CbCamera = ResourceDescriptorHeap[3];
	StructuredBuffer<ParticleData> ParticlesData = ResourceDescriptorHeap[7];
#endif

	float3 particleWPos = ParticlesData[instanceID].Position;
#ifdef DYNAMIC_RESOURCES
	float3 particleVPos = mul(CbCamera.View, float4(particleWPos, 1)).xyz;
#else
	float3 particleVPos = mul(View, float4(particleWPos, 1)).xyz;
#endif

	// billboard
	// 0 is left upper vertex
	// 1 is left lower vertex
	// 2 is right upper vertex
	// 3 is right lower vertex
	float viewLocalPosX = (0.5f - (vertexID >> 1)) * SPRITE_EXTENT;
	float viewLocalPosY = (-0.5f + (vertexID & 1)) * SPRITE_EXTENT;
	float3 vertexVPos = particleVPos + float3(viewLocalPosX, viewLocalPosY, 0);
#ifdef DYNAMIC_RESOURCES
	float4 vertexClipPos = mul(CbCamera.Proj, float4(vertexVPos, 1));
#else
	float4 vertexClipPos = mul(Proj, float4(vertexVPos, 1));
#endif

	return vertexClipPos;
}