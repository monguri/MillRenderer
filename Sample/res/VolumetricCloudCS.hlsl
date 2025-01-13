static const uint THREAD_GROUP_SIZE_X = 8;
static const uint THREAD_GROUP_SIZE_Y = 8;
static const float HALF_MAX = 65504;

#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(CBV(b1))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(UAV(u1))"\
", DescriptorTable(UAV(u2))"\

cbuffer CbCamera : register(b0)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbVolumetricCloud : register(b1)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
};

RWTexture2D<float4> OutCloudColor0 : register(u0);
RWTexture2D<float4> OutCloudColor1 : register(u1);
RWTexture2D<float4> OutCloudDepth : register(u2);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	[branch]
	if (all(DTid >= uint2(Width, Height)))
	{
		return;
	}

	float2 uv = (DTid + 0.5f) / float2(Width, Height);

	OutCloudColor0[DTid] = float4(1, 0, 0, 1);
	OutCloudColor1[DTid] = float4(0, 1, 0, 1);
	OutCloudDepth[DTid] = float4(HALF_MAX, HALF_MAX, HALF_MAX, HALF_MAX);
}