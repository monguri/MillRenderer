#include "BRDF.hlsli"
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

float3 IntegrateSpecularCube(in float3 V, in float3 N, in float a, in float width, in float mipCount)
{
	float3 acc = 0.0f;
	float accWeight = 0.0f;

	float omegaP = (4.0f * F_PI) / (6.0f * width * width);
	float bias = 1.0f;

	for (uint i = 0; i < SampleCount; ++i)
	{
		float2 u = Hammersley(i, SampleCount);

		float3 H = SampleGGX(u, a, N);

		float3 L = normalize(2 * dot(V, H) * H - V);

		float NdotL = saturate(dot(N, L));
		if (NdotL > 0.0f)
		{
#ifdef ENABLE_MIPMAP_FILTERING
			float pdf = D_GGX(NdotL, a) * NdotL;
			float omegaS = 1.0f / max(SampleCount * pdf, 1e-8f);
			float l = 0.5f * (log2(omegaS) - log2(omegaP)) + bias;
			float mipLevel = clamp(l, 0.0f, mipCount);

			acc += IBLCube.SampleLevel(IBLSmp, L, mipLevel).rgb * NdotL;
			accWeight += NdotL;
#else
			acc += IBLCube.Sample(IBLSmp, L).rgb * NdotL;
			accWeight += NdotL;
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
	float weight = 0.0f;

	float3 V = N;

	int sampleCount = 1024;
	for (int i = 0; i < sampleCount; i++)
	{
		float4 importanceSample = GetImportanceSample(i, sampleCount, N, 0.0f);
		float3 H = importanceSample.xyz;
		float pdf = importanceSample.w;

        // mipmap filtered samples (GPU Gems 3, 20.4)
		float lod = computeLod(pdf, sampleCount, Width);

		// Note: reflect takes incident vector.
		float3 L = normalize(reflect(-V, H));
		float NdotL = dot(N, L);

		if (NdotL > 0.0f)
		{
			if (Roughness == 0.0f)
			{
				// without this the roughness=0 lod is too high
				lod = 0.0f;
			}

			float3 sampleColor = IBLCube.SampleLevel(IBLSmp, L, 0.0f).rgb;
			color += sampleColor * NdotL;
			weight += NdotL;
		}
	}

	if (weight != 0.0f)
	{
		color /= weight;
	}

	return color;
}

float4 main(const VSOutput input) : SV_TARGET
{
	float3 output = 0;
	float3 dir = CalcDirection(input.TexCoord, FaceIndex);

#if 0
	if (Roughness == 0.0f)
	{
		output = IBLCube.SampleLevel(IBLSmp, dir, 0.0f).rgb;
	}
	else
	{
		output = IntegrateSpecularCube(dir, dir, Roughness, Width, MipCount); 
	}
#else
	output = filterColor(dir);
#endif

	return float4(output, 1.0f);
}