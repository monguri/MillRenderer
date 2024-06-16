cbuffer CbHZB : register(b0)
{
	int DstMip0Width : packoffset(c0);
	int DstMip0Height : packoffset(c0.y);
	float HeightScale : packoffset(c0.z);
	int NumOutputMip : packoffset(c0.w);
}

static const uint GROUP_TILE_SIZE = 8;

Texture2D DepthMap : register(t0);
SamplerState PointClampSmp : register(s0);

// the number of output textures need to match HCB_MAX_MIP_BATCH_SIZE of cpp.
RWTexture2D<float4> OutHCB_Mip0 : register(u0);
RWTexture2D<float4> OutHCB_Mip1 : register(u1);
RWTexture2D<float4> OutHCB_Mip2 : register(u2);
RWTexture2D<float4> OutHCB_Mip3 : register(u3);
RWTexture2D<float4> OutHCB_Mip4 : register(u4);

groupshared float SharedMaxDeviceZ[GROUP_TILE_SIZE * GROUP_TILE_SIZE];

[numthreads(GROUP_TILE_SIZE, GROUP_TILE_SIZE, 1)]
void main(uint2 GroupId : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupThreadIndex : SV_GroupIndex)
{
	float2 uv = (DTid + 0.5f) * float2(1, HeightScale) / float2(DstMip0Width, DstMip0Height);
	float4 deviceZ = DepthMap.GatherRed(PointClampSmp, uv, 0);
	float maxDeviceZ = max(deviceZ.x, max(deviceZ.y, max(deviceZ.z, deviceZ.w)));
	OutHCB_Mip0[DTid] = maxDeviceZ;

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
			uint2 OutputPixelPos = GroupId * tileSize + uint2(groupThreadIndex % tileSize, groupThreadIndex / tileSize);

			if (mipLevel == 1)
			{
				OutHCB_Mip1[OutputPixelPos] = tileMaxDeviceZ;
			}
			else if (mipLevel == 2)
			{
				OutHCB_Mip2[OutputPixelPos] = tileMaxDeviceZ;
			}
			else if (mipLevel == 3)
			{
				OutHCB_Mip3[OutputPixelPos] = tileMaxDeviceZ;
			}
			else if (mipLevel == 4)
			{
				OutHCB_Mip4[OutputPixelPos] = tileMaxDeviceZ;
			}

			SharedMaxDeviceZ[groupThreadIndex] = tileMaxDeviceZ;
		}
	}
}