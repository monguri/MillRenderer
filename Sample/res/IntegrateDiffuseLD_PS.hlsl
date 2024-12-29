#include "BakeUtil.hlsli"

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbBake : register(b0)
{
	int FaceIndex : packoffset(c0);
	float Roughness : packoffset(c0.y);
	float Width : packoffset(c0.z);
	float MipCount : packoffset(c0.w);
};

TextureCube IBLCube : register(t0);
SamplerState IBLSmp : register(s0);

float3 IntegrateDiffuseCube(in float3 N, in float width, in float mipCount)
{
	float3 acc = 0.0f;
	float accWeight = 0.0f;

	float omegaP = (4.0f * F_PI) / (6.0f * width * width);
	float bias = 1.0f;

	for (uint i = 0; i < SampleCount; ++i)
	{
		float2 u = Hammersley(i, SampleCount);

		float3 L = SampleLambert(u, N);
		float NdotL = saturate(dot(N, L));

		if (NdotL > 0.0f)
		{
#ifdef ENABLE_MIPMAP_FILTERING
			float pdf = NdotL / F_PI;
			float omegaS = 1.0f / max(SampleCount * pdf, 1e-8f);
			float l = 0.5f * (log2(omegaS) - log2(omegaP)) + bias;
			float mipLevel = clamp(l, 0.0f, mipCount);

			acc += IBLCube.SampleLevel(IBLSmp, L, mipLevel).rgb;
			accWeight += 1.0f;
#else
			acc += IBLCube.Sample(IBLSmp, L).rgb;
			accWeight += 1.0f;
#endif
		}
	}

	if (accWeight == 0.0f)
	{
		return acc;
	}
	else
	{
		return acc / accWeight;
	}
}

// Referenced glTF-Sample-Viewer ibl_filtering.frag
float3 filterColor(float3 N)
{
	float3 color = 0;

	float3 V = N;

	int sampleCount = 2048;
	for (int i = 0; i < sampleCount; i++)
	{
		float4 importanceSample = GetImportanceSample(i, sampleCount, N, 0.0f);
		float3 H = importanceSample.xyz;
		float pdf = importanceSample.w;

        // mipmap filtered samples (GPU Gems 3, 20.4)
		float lod = computeLod(pdf, sampleCount, Width);

		// sample lambertian at a lower resolution to avoid fireflies
		float3 lambertian = IBLCube.SampleLevel(IBLSmp, H, lod).rgb;

		//// the below operations cancel each other out
		// lambertian *= NdotH; // lamberts law
		// lambertian /= pdf; // invert bias from importance sampling
		// lambertian /= MATH_PI; // convert irradiance to radiance https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/

		color += lambertian;
	}

	color /= float(sampleCount);
	return color;
}

float4 main(const VSOutput input) : SV_TARGET
{
	float3 dir = CalcDirection(input.TexCoord, FaceIndex);
#if 1
	float3 output = IntegrateDiffuseCube(dir, Width, MipCount); 
#else
	output = filterColor(dir);
#endif
	return float4(output, 1.0f);
}