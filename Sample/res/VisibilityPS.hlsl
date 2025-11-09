struct MSOutput
{
	float4 position : SV_Position;
};

struct PrimitiveData
{
	uint primitiveID : SV_PrimitiveID;
	uint meshletID : MESHLET_ID;
};

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
	uint MaterialID;
};

float4 main() : SV_TARGET
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}