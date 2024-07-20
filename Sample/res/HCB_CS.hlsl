#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(SRV(t0))"\
", DescriptorTable(UAV(u0))"\
", DescriptorTable(UAV(u1))"\
", DescriptorTable(UAV(u2))"\
", DescriptorTable(UAV(u3))"\
", DescriptorTable(UAV(u4))"\
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

static const uint GROUP_TILE_SIZE = 1 << (5 - 1);

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

// the number of output textures need to match HCB_MAX_MIP_BATCH_SIZE of cpp.
RWTexture2D<float4> OutHCB_Mip0 : register(u0);
RWTexture2D<float4> OutHCB_Mip1 : register(u1);
RWTexture2D<float4> OutHCB_Mip2 : register(u2);
RWTexture2D<float4> OutHCB_Mip3 : register(u3);
RWTexture2D<float4> OutHCB_Mip4 : register(u4);

groupshared float4 SharedMemory[GROUP_TILE_SIZE * GROUP_TILE_SIZE];

[RootSignature(ROOT_SIGNATURE)]
[numthreads(GROUP_TILE_SIZE, GROUP_TILE_SIZE, 1)]
void main(uint2 GroupId : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupThreadIndex : SV_GroupIndex)
{
	float2 uv = (DTid + 0.5f) * float2(1, HeightScale) / float2(DstMip0Width, DstMip0Height);
	float4 color = ColorMap.SampleLevel(PointClampSmp, uv, 0);
	OutHCB_Mip0[DTid] = color;

	SharedMemory[groupThreadIndex] = color;

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
			float4 color = SharedMemory[groupThreadIndex * 2];
			float4 rightPixelColor = SharedMemory[groupThreadIndex * 2 + 1];
			float4 bottomPixelColor = SharedMemory[groupThreadIndex * 2 +  + parentTileSize];
			float4 rightBottomPixelColor = SharedMemory[groupThreadIndex * 2 +  + parentTileSize + 1];

			float3 avgColor = (color.rgb + rightPixelColor.rgb + bottomPixelColor.rgb + rightBottomPixelColor.rgb) / 4;
			uint2 OutputPixelPos = GroupId * tileSize + uint2(groupThreadIndex % tileSize, groupThreadIndex / tileSize);

			if (mipLevel == 1)
			{
				OutHCB_Mip1[OutputPixelPos] = float4(avgColor, 1);
			}
			else if (mipLevel == 2)
			{
				OutHCB_Mip2[OutputPixelPos] = float4(avgColor, 1);
			}
			else if (mipLevel == 3)
			{
				OutHCB_Mip3[OutputPixelPos] = float4(avgColor, 1);
			}
			else if (mipLevel == 4)
			{
				OutHCB_Mip4[OutputPixelPos] = float4(avgColor, 1);
			}

			SharedMemory[groupThreadIndex] = float4(avgColor, 1);
		}
	}
}