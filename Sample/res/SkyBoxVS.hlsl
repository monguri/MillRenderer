struct VSInput
{
	float3 Position : POSITION;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 ClipPosition : CLIP_POSITION;
};

cbuffer CbSkyBox : register(b0)
{
	float4x4 WVP : packoffset(c0);
	float4x4 InvVRotP : packoffset(c4);
	float4x4 SkyViewLutReferential : packoffset(c8);
	float3 AtmosphereLightDirection : packoffset(c12);
	float ViewHeight : packoffset(c12.w);
	float3 AtmosphereLightLuminance : packoffset(c13);
	int SkyViewLutWidth : packoffset(c13.w);
	int SkyViewLutHeight : packoffset(c14);
	float BottomRadiusKm : packoffset(c14.y);
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	output.Position = mul(WVP, float4(input.Position, 1.0f));
	output.ClipPosition = output.Position;

	return output;
}