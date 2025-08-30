//TODO: hlsl内RootSignature定義はどう書けばいいかわからないのでとりあえずやらない
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

struct Payload
{
	float3 color;
};

[shader("raygeneration")]
void rayGeneration()
{
	// (Width, Height, 1)のレイ本数をそのままスクリーンのピクセルに割り当てる
	uint2 rayIndex = DispatchRaysIndex().xy;
	uint2 screenDim = DispatchRaysDimensions().xy;

	float2 normalXY = float2(rayIndex) / float2(screenDim) * 2 - 1;
	float aspectRatio = screenDim.y / screenDim.x;

	RayDesc rayDesc;
	// Triangleをちょうどいいカメラ位置で表示する
	rayDesc.Origin = float3(0, 0, -2);
	rayDesc.Direction = normalize(float3(normalXY.x, normalXY.y * aspectRatio, 1));

	rayDesc.TMin = 0;
	rayDesc.TMax = 100000;

	Payload payload;
	uint rayFlags = 0;
	uint instanceInclusionsMask = 0xFF;
	uint rayContributionToHitGroupIndex = 0;
	uint multiplierForGeometryContributionToHitGroupIndex = 0;
	uint missShaderIndex = 0;
	TraceRay(gRtAS, rayFlags, instanceInclusionsMask, rayContributionToHitGroupIndex, multiplierForGeometryContributionToHitGroupIndex, missShaderIndex, rayDesc, payload);

	float3 color = linearToSrgb(payload.color);
	gOutputTex[rayIndex.xy] = float4(color, 1);
}

[shader("miss")]
void miss(inout Payload payload)
{
	// light green
	payload.color = float3(0.4, 0.6, 0.2);
}

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrs)
{
	payload.color =
		float3(1, 0, 0) * (1 - attrs.barycentrics.x - attrs.barycentrics.y)
		+ float3(0, 1, 0) * attrs.barycentrics.x
		+ float3(0, 0, 1) * attrs.barycentrics.y;
}