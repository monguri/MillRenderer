//TODO: hlsl内RootSignature定義はどう書けばいいかわからないのでとりあえずやらない
struct MeshVertex
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
};

RaytracingAccelerationStructure RtAS : register(t0);
StructuredBuffer<MeshVertex> VB : register(t1);
StructuredBuffer<uint> IB : register(t2);
Texture2D<float4> BaseColorMap : register(t3);
RWTexture2D<float4> OutTex : register(u0);

SamplerState PointClampSmp : register(s0);

struct [raypayload] Payload
{
	float3 color : read(caller) : write(closesthit, miss);
};

[shader("raygeneration")]
void rayGeneration()
{
	// (Width, Height, 1)のレイ本数をそのままスクリーンのピクセルに割り当てる
	uint2 rayIndex = DispatchRaysIndex().xy;
	uint2 screenDim = DispatchRaysDimensions().xy;

	float2 normalXY = float2(rayIndex) / float2(screenDim) * 2 - 1;
	float aspectRatio = float(screenDim.y) / float(screenDim.x);

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

	TraceRay(RtAS, rayFlags, instanceInclusionsMask, rayContributionToHitGroupIndex, multiplierForGeometryContributionToHitGroupIndex, missShaderIndex, rayDesc, payload);

	OutTex[rayIndex.xy] = float4(payload.color, 1);

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
	uint primitiveIndex = PrimitiveIndex();
	uint index0 = IB[primitiveIndex * 3 + 0];
	uint index1 = IB[primitiveIndex * 3 + 1];
	uint index2 = IB[primitiveIndex * 3 + 2];

	float2 uv0 = VB[index0].TexCoord;
	float2 uv1 = VB[index1].TexCoord;
	float2 uv2 = VB[index2].TexCoord;

	float2 uv = uv0 * (1 - attrs.barycentrics.x - attrs.barycentrics.y)
		+ uv1 * attrs.barycentrics.x
		+ uv2 * attrs.barycentrics.y;

	float4 baseColor = BaseColorMap.SampleLevel(PointClampSmp, uv, 0);
	payload.color = baseColor.rgb;
}
