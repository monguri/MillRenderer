cbuffer CbHZB : register(b0)
{
	int DstMip0Width;
	int DstMip0Height;
	float HeightScale;
}

static const uint GROUP_TILE_SIZE = 8;

Texture2D DepthMap : register(t0);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float> OutHZB_Mip0 : register(u0);
RWTexture2D<float> OutHZB_Mip1 : register(u1);
RWTexture2D<float> OutHZB_Mip2 : register(u2);
RWTexture2D<float> OutHZB_Mip3 : register(u3);

[numthreads(GROUP_TILE_SIZE, GROUP_TILE_SIZE, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid + 0.5f) * float2(1, HeightScale) / float2(DstMip0Width, DstMip0Height);
	float4 deviceZ = DepthMap.GatherRed(PointClampSmp, uv, 0);
	float maxDeviceZ = max(deviceZ.x, max(deviceZ.y, max(deviceZ.z, deviceZ.w)));
	OutHZB_Mip0[DTid] = maxDeviceZ;
	OutHZB_Mip1[DTid >> 1] = maxDeviceZ;
	OutHZB_Mip2[DTid >> 2] = maxDeviceZ;
	OutHZB_Mip3[DTid >> 3] = maxDeviceZ;
}