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
	float4x4 World : packoffset(c0);
	float4x4 View : packoffset(c4);
	float4x4 Proj : packoffset(c8);
	int TexWidth :  packoffset(c12);
	int TexHeight :  packoffset(c12.y);
}

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