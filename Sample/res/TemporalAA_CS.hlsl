cbuffer CbTemporalAA : register(b0)
{
	int Width;
	int Height;
	int bEnableTemporalAA;
	float4x4 ClipToPrevClip;
}

Texture2D DepthMap : register(t0);
Texture2D ColorMap : register(t1);
Texture2D HitoryMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float2 uv = (DTid.xy + 0.5f) / float2(Width, Height);
	// [0, 1] to [-1, 1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.SampleLevel(PointClampSmp, uv, 0).r;

	float4 clipPos = float4(screenPos, deviceZ, 1);
	//float4 prevClipPos = mul(clipPos, ClipToPrevClip); // TODO:mul invert?
	float4 prevClipPos = mul(ClipToPrevClip, clipPos); // TODO:mul invert?
	float2 prevScreenPos = prevClipPos.xy / prevClipPos.w;
	float2 prevUV = prevScreenPos * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

	float3 curColor = ColorMap.SampleLevel(PointClampSmp, uv, 0).rgb;
	float3 histColor = HitoryMap.SampleLevel(PointClampSmp, prevUV, 0).rgb;

	if (bEnableTemporalAA)
	{
		// TODO: just a average
		OutResult[DTid.xy] = float4((curColor + histColor) * 0.5f, 1.0f);
	}
	else
	{
		// just copy
		OutResult[DTid.xy] = float4(curColor, 1.0f);
	}
}