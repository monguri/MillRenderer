struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbCameraVelocity : register(b0)
{
	float4x4 ClipToPrevClip;
}

Texture2D ColorMap : register(t0);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float colorR = ColorMap.Sample(PointClampSmp, input.TexCoord).x;
	return float4(colorR, 0.0f, 0.0f, 1.0f);
}