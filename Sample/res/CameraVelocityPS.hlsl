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
Texture2D DepthMap : register(t1);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	// [0, 1] to [-1, 1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;

	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, ndcPos);
	float4 prevNdcPos = prevClipPos / prevClipPos.w;

	return float4(ndcPos.xyz - prevNdcPos.xyz, 1);
}