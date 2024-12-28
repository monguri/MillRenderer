struct VSInput
{
	float3 Position : POSITION;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

cbuffer CbSkyAtmosphere
{
	float4x4 World : packoffset(c0);
	float4x4 View : packoffset(c4);
	float4x4 Proj : packoffset(c8);
	float4x4 SkyViewLutReferential : packoffset(c12);
	float3 CameraVector : packoffset(c16);
	int SkyViewLutWidth : packoffset(c16.w);
	int SkyViewLutHeight : packoffset(c17);
	float BottomRadiusKm : packoffset(c17.y);
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float4 cameraWorldPos = mul(World, float4(0, 0, 0, 1));

	float4 worldPos = mul(World, float4(input.Position, 1.0f));
	float4 viewPos = mul(View, worldPos);
	float4 projPos = mul(Proj, viewPos);

	output.Position = projPos;
	output.TexCoord = worldPos.xyz - cameraWorldPos.xyz;

	return output;
}