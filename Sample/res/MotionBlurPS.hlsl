#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

#define MAX_SAMPLE_COUNT 10
#define SAMPLE_PIXEL_WIDTH 3.0f
#define MAX_SPEED (MAX_SAMPLE_COUNT * SAMPLE_PIXEL_WIDTH + 2)
#define MIN_SPEED (SAMPLE_PIXEL_WIDTH + 1)
#define SPEED_WEIGHT_MULTIPLIER (MAX_SPEED / MIN_SPEED)

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbMotionBlur : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
	float Scale : packoffset(c0.z);
}

Texture2D ColorMap : register(t0);
Texture2D VelocityMap : register(t1);
SamplerState PointClampSmp : register(s0);

float GetSampleWeight(float2 uv)
{
	float2 velocityUV = VelocityMap.SampleLevel(PointClampSmp, uv, 0).xy;
	// velocity of pixel space [0, 0]*[Width, Height]. y direction is same as V direction.
	float2 velocityPS = velocityUV * float2(Width, Height);
	float speedPS = length(velocityPS);

	// if speedPS of this pixel less than MIN_SPEED, no motion blur, but other sample can be more slow.
	return saturate(speedPS * SPEED_WEIGHT_MULTIPLIER);
}

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float2 velocityUV = VelocityMap.SampleLevel(PointClampSmp, input.TexCoord, 0).xy;
	// velocity of pixel space [0, 0]*[Width, Height]. y direction is same as V direction.
	float2 velocityPS = velocityUV * float2(Width, Height) * Scale;
	float speedPS = length(velocityPS);

	float halfSampleCount = min(MAX_SAMPLE_COUNT * 0.5f, speedPS / SAMPLE_PIXEL_WIDTH * 0.5f);

	float2 deltaUV = (velocityPS / speedPS) * (SAMPLE_PIXEL_WIDTH / float2(Width, Height));
	float2 uv1 = input.TexCoord;
	float2 uv2 = input.TexCoord;


	float4 result = ColorMap.Sample(PointClampSmp, input.TexCoord);

	if (speedPS >= MIN_SPEED)
	{
		// accum.a becomes accumulation of weight.
		float4 accum = float4(result.rgb, 1);

		for (float i = halfSampleCount - 1.0; i > 0.0; i -= 1.0)
		{
			uv1 += deltaUV;
			accum += float4(ColorMap.SampleLevel(PointClampSmp, uv1, 0).rgb * GetSampleWeight(uv1), 1);

			uv2 -= deltaUV;
			accum += float4(ColorMap.SampleLevel(PointClampSmp, uv2, 0).rgb * GetSampleWeight(uv2), 1);
		}

		// almost the same as frac(halfSampleCount) replaces 0 with 1.
		float remainder = 1 + halfSampleCount - ceil(halfSampleCount);
		deltaUV *= remainder;

		uv1 += deltaUV;
		accum += float4(ColorMap.SampleLevel(PointClampSmp, uv1, 0).rgb * GetSampleWeight(uv1), 1) * remainder;

		uv2 -= deltaUV;
		accum += float4(ColorMap.SampleLevel(PointClampSmp, uv2, 0).rgb * GetSampleWeight(uv2), 1) * remainder;

		// lerp with alpha of speed.
		result = lerp(result, accum / accum.a, saturate(speedPS / MAX_SPEED));
	}

	return result;
}
