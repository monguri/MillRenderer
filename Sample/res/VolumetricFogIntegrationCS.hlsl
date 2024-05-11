// It must be equal to the value used in cpp.
static const uint THREAD_GROUP_SIZE_XYZ = 4;

cbuffer CbVolumetricFog : register(b0)
{
	float4x4 InvVRotPMatrix : packoffset(c0);
	int3 GridSize : packoffset(c4);
	float Near : packoffset(c4.w);
	float Far : packoffset(c5);
}

Texture2D DepthMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture3D<float4> OutResult : register(u0);

[numthreads(THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ, THREAD_GROUP_SIZE_XYZ)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	OutResult[DTid] = float4(1, 0, 0, 0);
}