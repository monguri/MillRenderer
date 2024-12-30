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
	float ViewHeight : packoffset(c12);
	int SkyViewLutWidth : packoffset(c12.y);
	int SkyViewLutHeight : packoffset(c12.z);
	float BottomRadiusKm : packoffset(c12.w);
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	output.Position = mul(WVP, float4(input.Position, 1.0f));
	output.ClipPosition = output.Position;

	return output;
}