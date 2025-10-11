//#define USE_MANUAL_PCF_FOR_SHADOW_MAP
#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP

// referenced UE ShadowFilteringCommon.ush
float4 CalculateOcclusion(float4 shadowMapDepth, float sceneDepth, float transitionScale)
{
	// The standard comparison is SceneDepth < ShadowmapDepth
	// Using a soft transition based on depth difference
	// Offsets shadows a bit but reduces self shadowing artifacts considerably

	// Unoptimized Math: saturate((Settings.SceneDepth - ShadowmapDepth) * TransitionScale + 1);
	// Rearranged the math so that per pixel constants can be optimized from per sample constants.
	return saturate((sceneDepth - shadowMapDepth) * transitionScale + 1);
}

// horizontal PCF, input 6x2
float2 HorizontalPCF5x2(float2 fraction, float4 values00, float4 values20, float4 values40)
{
	float result0;
	float result1;

	result0 = values00.w * (1.0 - fraction.x);
	result1 = values00.x * (1.0 - fraction.x);
	result0 += values00.z;
	result1 += values00.y;
	result0 += values20.w;
	result1 += values20.x;
	result0 += values20.z;
	result1 += values20.y;
	result0 += values40.w;
	result1 += values40.x;
	result0 += values40.z * fraction.x;
	result1 += values40.y * fraction.x;

	return float2(result0, result1);
}

// PCF falloff is overly blurry, apply to get
// a more reasonable look artistically. Should not be applied to
// other passes that target pbr (e.g., raytracing and denoising).
float ApplyPCFOverBlurCorrection(float occlusion)
{
	return occlusion * occlusion;
}

float GetShadowMultiplier(Texture2D shadowMap,
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
						SamplerComparisonState shadowSmp,
#else
						SamplerState shadowSmp,
#endif
						float2 shadowMapSize, float3 shadowCoord, float transitionScale)
{
#ifdef USE_MANUAL_PCF_FOR_SHADOW_MAP
	// referenced UE Manual5x5PCF() of ShadowFilteringCommon.ush
	// high quality, 6x6 samples, using gather4
	float2 texelPos = shadowCoord.xy * shadowMapSize.x - 0.5f;	// bias to be consistent with texture filtering hardware
	float2 fraction = frac(texelPos);
	// Gather4 samples "at the following locations: (-,+),(+,+),(+,-),(-,-), where the magnitude of the deltas are always half a texel" - DX11 Func. Spec.
	// So we need to offset to the centre of the 2x2 grid we want to sample.
	float2 samplePos = (floor(texelPos) + 1.0f) * shadowMapSize.y;

	// transisionScale is considered NdotL, but use fixed value at pixel offset.
	// shadowCoord.z is fixed too.
	float result = 0.0f;
	{
		float4 values00 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(-2, -2)), shadowCoord.z, transitionScale);
		float4 values20 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(0, -2)), shadowCoord.z, transitionScale);
		float4 values40 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(2, -2)), shadowCoord.z, transitionScale);

		float2 row0 = HorizontalPCF5x2(fraction, values00, values20, values40);
		result += row0.x * (1.0f - fraction.y) + row0.y;
	}

	{
		float4 values02 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(-2, 0)), shadowCoord.z, transitionScale);
		float4 values22 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(0, 0)), shadowCoord.z, transitionScale);
		float4 values42 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(2, 0)), shadowCoord.z, transitionScale);

		float2 row1 = HorizontalPCF5x2(fraction, values02, values22, values42);
		result += row1.x + row1.y;
	}

	{
		float4 values04 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(-2, 2)), shadowCoord.z, transitionScale);
		float4 values24 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(0, 2)), shadowCoord.z, transitionScale);
		float4 values44 = CalculateOcclusion(shadowMap.Gather(shadowSmp, samplePos, int2(2, 2)), shadowCoord.z, transitionScale);

		float2 row2 = HorizontalPCF5x2(fraction, values04, values24, values44);
		result += row2.x + row2.y * fraction.y;
	}

	result /= 25;
	result = ApplyPCFOverBlurCorrection(result);
	return (1 - result);
#else // USE_MANUAL_PCF_FOR_SHADOW_MAP
	#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#ifdef SINGLE_SAMPLE_SHADOW_MAP
		float result = shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy, shadowCoord.z);
		#else // SINGLE_SAMPLE_SHADOW_MAP
		const float Dilation = 2.0f;
		float d1 = Dilation * shadowMapSize.y * 0.125f;
		float d2 = Dilation * shadowMapSize.y * 0.875f;
		float d3 = Dilation * shadowMapSize.y * 0.625;
		float d4 = Dilation * shadowMapSize.y * 0.375;
		float result = (2.0f * shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy, shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(-d2, d1), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(-d1, d2), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(d2, d1), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(d1, d2), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(-d4, d3), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(-d3, d4), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(d4, d3), shadowCoord.z)
			+ shadowMap.SampleCmpLevelZero(shadowSmp, shadowCoord.xy + float2(d3, d4), shadowCoord.z)) / 10.0f;
		#endif // SINGLE_SAMPLE_SHADOW_MAP
	#else // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
		#ifdef SINGLE_SAMPLE_SHADOW_MAP
		float shadowVal = shadowMap.Sample(shadowSmp, shadowCoord.xy).x;
		float result = 1.0f;
		if (shadowCoord.z > shadowVal)
		{
			result = 0.0f;
		}
		#else // SINGLE_SAMPLE_SHADOW_MAP
		const float Dilation = 2.0f;
		float d1 = Dilation * shadowMapSize.y * 0.125f;
		float d2 = Dilation * shadowMapSize.y * 0.875f;
		float d3 = Dilation * shadowMapSize.y * 0.625;
		float d4 = Dilation * shadowMapSize.y * 0.375;
		float result = (2.0f * ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(-d2, d1)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(-d1, d2)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(d2, d1)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(d1, d2)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(-d4, d3)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(-d3, d4)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(d4, d3)).x) ? 0.0f : 1.0f)
			+ ((shadowCoord.z > shadowMap.Sample(shadowSmp, shadowCoord.xy + float2(d3, d4)).x) ? 0.0f : 1.0f)) / 10.0f;
		#endif // SINGLE_SAMPLE_SHADOW_MAP
	#endif // USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP

	return result * result;
#endif // USE_MANUAL_PCF_FOR_SHADOW_MAP
}
