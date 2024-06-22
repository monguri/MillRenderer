cbuffer CbSSGI : register(b0)
{
	float4x4 ViewMatrix : packoffset(c0);
	int Width : packoffset(c4);
	int Height : packoffset(c4.y);
}

Texture2D HCB : register(t0);
Texture2D HZB : register(t1);
Texture2D NormalMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 4;
static const uint TILE_PIXEL_SIZE_Y = 4;
static const uint CONFIG_RAY_COUNT = 16;
static const uint TILE_PIXEL_COUNT = TILE_PIXEL_SIZE_X * TILE_PIXEL_SIZE_Y;
static const uint LANE_PER_GROUPS = TILE_PIXEL_COUNT * CONFIG_RAY_COUNT;

groupshared float4 SharedMemory[TILE_PIXEL_COUNT];

// TODO: same as the function of SSR_PS.hlsl
float3 GetWSNormal(float2 uv)
{
	return normalize(NormalMap.Sample(PointClampSmp, uv).xyz * 2.0f - 1.0f);
}

// TODO: same as the function of SSR_PS.hlsl
float GetHZBDeviceZ(float2 uv, float mipLevel)
{
	// HZB's uv is scaled to keep aspect ratio.
	return HZB.SampleLevel(PointClampSmp, uv * float2(1, (float)Height / Width), mipLevel).r;
}

[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, CONFIG_RAY_COUNT)]
void main(uint2 DTid : SV_DispatchThreadID, uint2 GroupId : SV_GroupID, uint GroupThreadIndex : SV_GroupIndex)
{
	uint GroupPixelId = GroupThreadIndex % TILE_PIXEL_COUNT;
	uint RaySequenceId = GroupThreadIndex / TILE_PIXEL_COUNT;

	// Store 
	if (RaySequenceId == 0)
	{
		// TODO: Convert ViewSpace Normal
		float2 rcpDimension = 1.0f / float2(Width, Height);
		float2 uv = (DTid + 0.5f) * rcpDimension;

		float3 worldNormal = GetWSNormal(uv);
		float3 viewNormal = normalize(mul((float3x3)ViewMatrix, worldNormal));
		SharedMemory[GroupPixelId].xyz = viewNormal;

		float deviceZ = GetHZBDeviceZ(uv, 0);
		SharedMemory[GroupPixelId].w = deviceZ;
	}

	GroupMemoryBarrierWithGroupSync();

	OutResult[DTid] = float4(0, 0, 0, 0);
}