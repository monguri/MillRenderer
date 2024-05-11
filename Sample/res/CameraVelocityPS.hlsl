struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbCameraVelocity : register(b0)
{
	float4x4 ClipToPrevClip : packoffset(c0);
}

Texture2D DepthMap : register(t0);
Texture2D ObjectVelocityMap : register(t1);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 objectVelocity = ObjectVelocityMap.Sample(PointClampSmp, input.TexCoord).rg;
	if (abs(objectVelocity.r) + abs(objectVelocity.g) > 0.0f) // TODO: should consider error?
	{
		return float4(objectVelocity, 0, 1);
	}

	// [0, 1] to [-1, 1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;

	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, ndcPos);
	float4 prevNdcPos = prevClipPos / prevClipPos.w;
	float2 prevUV = (prevNdcPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	return float4(input.TexCoord - prevUV, 0, 1);
}