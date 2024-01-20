#define MAX_SAMPLE_COUNT 10
#define SAMPLE_PIXEL_WIDTH 3.0f

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbMotionBlur : register(b0)
{
	int Width;
	int Height;
}

Texture2D ColorMap : register(t0);
Texture2D VelocityMap : register(t1);
SamplerState PointClampSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 velocityUV = VelocityMap.Sample(PointClampSmp, input.TexCoord).xy;
	// velocity of pixel space [0, 0]*[Width, Height]. y direction is same as V direction.
	float2 velocityPS = velocityUV * float2(Width, Height);
	float speedPS = length(velocityPS);

	float halfSampleCount = min(MAX_SAMPLE_COUNT * 0.5f, speedPS / SAMPLE_PIXEL_WIDTH * 0.5f);

	float2 deltaUV = (velocityPS / speedPS) * (SAMPLE_PIXEL_WIDTH / float2(Width, Height));
	float2 uv1 = input.TexCoord;
	float2 uv2 = input.TexCoord;

	float3 accum = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
	float sampleCount = 1;

	for (float i = halfSampleCount - 1.0; i > 0.0; i -= 1.0)
	{
		accum += ColorMap.Sample(PointClampSmp, uv1 += deltaUV).rgb;
		accum += ColorMap.Sample(PointClampSmp, uv2 -= deltaUV).rgb;
		sampleCount += 2;
	}

	// almost the same as frac(halfSampleCount) replaces 0 with 1
	float remainder = 1 + halfSampleCount - ceil(halfSampleCount);
	deltaUV *= remainder;
	accum += ColorMap.Sample(PointClampSmp, uv1 + deltaUV).rgb * remainder;
	accum += ColorMap.Sample(PointClampSmp, uv2 - deltaUV).rgb * remainder;
	sampleCount += (remainder * 2);

	float3 result = accum / sampleCount;
	return float4(result, 1);
}
