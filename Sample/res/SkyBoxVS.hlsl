struct VSInput
{
	float3 Position : POSITION;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

cbuffer CbSkyBox : register(b0)
{
	float4x4 WVP : packoffset(c0);
	float4x4 SkyViewLutReferential : packoffset(c4);
	float3 CameraVector : packoffset(c8);
	float ViewHeight : packoffset(c8.w);
	int SkyViewLutWidth : packoffset(c9);
	int SkyViewLutHeight : packoffset(c9.y);
	float BottomRadiusKm : packoffset(c9.z);
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	output.Position = mul(WVP, float4(input.Position, 1.0f));

	return output;
}