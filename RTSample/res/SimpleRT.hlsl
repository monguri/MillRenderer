RaytracingAccelerationStructure gRtAS : register(t0);
RWTexture2D<float4> gOutputTex : register(u0);

float3 linearToSrgb(float3 color)
{
    // http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	float3 sq1 = sqrt(color);
	float3 sq2 = sqrt(sq1);
	float3 sq3 = sqrt(sq2);
	float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * color;
	return srgb;
}

[shader("raygeneration")]
void rayGeneration()
{
	uint3 rayIndex = DispatchRaysIndex();
	float3 color = linearToSrgb(float3(0.4, 0.6, 0.2));
	gOutputTex[rayIndex.xy] = float4(color, 1);
}

struct Payload
{
	bool hit;
};

[shader("miss")]
void miss(inout Payload payload)
{
	payload.hit = false;
}

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrs)
{
	payload.hit = true;
}