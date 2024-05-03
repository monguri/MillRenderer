cbuffer CbHZB : register(b0)
{
	int DstMip0Width;
	int DstMip0Height;
	float HeightScale;
	int NumOutputMip;
}

static const uint GROUP_TILE_SIZE = 8;

Texture2D ParentTextureMip : register(t0);
SamplerState PointClampSmp : register(s0);

// the number of output textures need to match HZB_MAX_MIP_BATCH_SIZE of cpp.
RWTexture2D<float> OutHZB_Mip0 : register(u0);
RWTexture2D<float> OutHZB_Mip1 : register(u1);
RWTexture2D<float> OutHZB_Mip2 : register(u2);
RWTexture2D<float> OutHZB_Mip3 : register(u3);

groupshared float SharedMaxDeviceZ[GROUP_TILE_SIZE * GROUP_TILE_SIZE];

[numthreads(GROUP_TILE_SIZE, GROUP_TILE_SIZE, 1)]
void main(uint2 DTid : SV_DispatchThreadID, uint groupThreadIndex : SV_GroupIndex)
{
	float2 uv = (DTid + 0.5f) * float2(1, HeightScale) / float2(DstMip0Width, DstMip0Height);
	float4 deviceZ = ParentTextureMip.GatherRed(PointClampSmp, uv, 0);
	float maxDeviceZ = max(deviceZ.x, max(deviceZ.y, max(deviceZ.z, deviceZ.w)));
	uint2 OutputPixelPos = DTid;
	OutHZB_Mip0[OutputPixelPos] = maxDeviceZ;

	SharedMaxDeviceZ[groupThreadIndex] = maxDeviceZ;

	// the number of output textures need to match HZB_MAX_MIP_BATCH_SIZE of cpp.
	for (uint mipLevel = 1; mipLevel < (uint)NumOutputMip; mipLevel++)
	{
		// easier alghorithm than UE's HZB.usf
		GroupMemoryBarrierWithGroupSync();

		uint parentTileSize = uint(GROUP_TILE_SIZE) >> (mipLevel - 1);
		uint tileSize = uint(GROUP_TILE_SIZE) >> mipLevel;
		uint reduceBankSize = tileSize * tileSize;

		if (groupThreadIndex < reduceBankSize)
		{
			float4 parentMaxDeviceZ;
			parentMaxDeviceZ.x = SharedMaxDeviceZ[groupThreadIndex * 2];
			parentMaxDeviceZ.y = SharedMaxDeviceZ[groupThreadIndex * 2 + 1];
			parentMaxDeviceZ.z = SharedMaxDeviceZ[groupThreadIndex * 2 + parentTileSize];
			parentMaxDeviceZ.w = SharedMaxDeviceZ[groupThreadIndex * 2 + parentTileSize + 1];

			float tileMaxDeviceZ = max(parentMaxDeviceZ.x, max(parentMaxDeviceZ.y, max(parentMaxDeviceZ.z, parentMaxDeviceZ.w)));
			OutputPixelPos >>= 1;

			if (mipLevel == 1)
			{
				OutHZB_Mip1[OutputPixelPos] = tileMaxDeviceZ;
			}
			else if (mipLevel == 2)
			{
				OutHZB_Mip2[OutputPixelPos] = tileMaxDeviceZ;
			}
			else if (mipLevel == 3)
			{
				OutHZB_Mip3[OutputPixelPos] = tileMaxDeviceZ;
			}

			SharedMaxDeviceZ[groupThreadIndex] = tileMaxDeviceZ;
		}
	}
}