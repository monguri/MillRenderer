#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(UAV(u1))"\
", DescriptorTable(UAV(u2))"\
", DescriptorTable(UAV(u3))"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
")"\

cbuffer CbHZB : register(b0)
{
	int DstMip0Width : packoffset(c0);
	int DstMip0Height : packoffset(c0.y);
	float HeightScale : packoffset(c0.z);
	int NumOutputMip : packoffset(c0.w);
}

static const uint GROUP_TILE_SIZE = 1 << (4 - 1);

Texture2D ParentTextureMip : register(t0);
SamplerState PointClampSmp : register(s0);

// the number of output textures need to match HZB_MAX_MIP_BATCH_SIZE of cpp.
RWTexture2D<float> OutHZB_Mip0 : register(u0);
RWTexture2D<float> OutHZB_Mip1 : register(u1);
RWTexture2D<float> OutHZB_Mip2 : register(u2);
RWTexture2D<float> OutHZB_Mip3 : register(u3);

// Most minimum deviceZ is furthest because we use reverse z for scene depth.
groupshared float SharedMinDeviceZ[GROUP_TILE_SIZE * GROUP_TILE_SIZE];

[RootSignature(ROOT_SIGNATURE)]
[numthreads(GROUP_TILE_SIZE, GROUP_TILE_SIZE, 1)]
void main(uint2 GroupId : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupThreadIndex : SV_GroupIndex)
{
	float2 uv = (DTid + 0.5f) * float2(1, HeightScale) / float2(DstMip0Width, DstMip0Height);
	float4 deviceZ = ParentTextureMip.GatherRed(PointClampSmp, uv, 0);
	float minDeviceZ = min(deviceZ.x, min(deviceZ.y, min(deviceZ.z, deviceZ.w)));
	OutHZB_Mip0[DTid] = minDeviceZ;

	SharedMinDeviceZ[groupThreadIndex] = minDeviceZ;

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
			float4 parentMinDeviceZ;
			parentMinDeviceZ.x = SharedMinDeviceZ[groupThreadIndex * 2];
			parentMinDeviceZ.y = SharedMinDeviceZ[groupThreadIndex * 2 + 1];
			parentMinDeviceZ.z = SharedMinDeviceZ[groupThreadIndex * 2 + parentTileSize];
			parentMinDeviceZ.w = SharedMinDeviceZ[groupThreadIndex * 2 + parentTileSize + 1];

			float tileMinDeviceZ = min(parentMinDeviceZ.x, min(parentMinDeviceZ.y, min(parentMinDeviceZ.z, parentMinDeviceZ.w)));
			uint2 OutputPixelPos = GroupId * tileSize + uint2(groupThreadIndex % tileSize, groupThreadIndex / tileSize);

			if (mipLevel == 1)
			{
				OutHZB_Mip1[OutputPixelPos] = tileMinDeviceZ;
			}
			else if (mipLevel == 2)
			{
				OutHZB_Mip2[OutputPixelPos] = tileMinDeviceZ;
			}
			else if (mipLevel == 3)
			{
				OutHZB_Mip3[OutputPixelPos] = tileMinDeviceZ;
			}

			SharedMinDeviceZ[groupThreadIndex] = tileMinDeviceZ;
		}
	}
}