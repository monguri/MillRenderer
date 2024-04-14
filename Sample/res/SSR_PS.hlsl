struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbCameraVelocity : register(b0)
{
	float Parameter;
}

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture2D NormalMap : register(t2);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	return float4(Color, 1.0f);
}