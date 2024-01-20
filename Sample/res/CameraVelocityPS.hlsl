struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbCameraVelocity : register(b0)
{
	float4x4 ClipToPrevClip;
	int Width;
	int Height;
}

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 uv = input.TexCoord + 0.5f / float2(Width, Height); // half pixel offset
	// [0, 1] to [-1, 1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.SampleLevel(PointClampSmp, uv, 0).r;

	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, ndcPos);
	float4 prevNdcPos = prevClipPos / prevClipPos.w;

	return float4(ndcPos.xyz - prevNdcPos.xyz, 1);
}