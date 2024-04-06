#include "BakeUtil.hlsli"

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

float3 IntegrateDFG_Only(float NdotV, float roughness)
{
	float3 N = float3(0.0f, 0.0f, 1.0f);
	float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
	float a = roughness * roughness;

	float3 acc = 0;
	const uint count = 1024;

	for (uint i = 0; i < count; ++i)
	{
		float2 u = Hammersley(i, SampleCount);
		float3 H = SampleGGX(u, a, N);
		float3 L = normalize(2 * dot(V, H) * H - V);
		float NdotL = dot(N, L);

		if (NdotL > 0.0f)
		{
			float NdotH = saturate(dot(N, H));
			float VdotH = saturate(dot(V, H));

			float V = V_GGX(NdotL, NdotV, a) * NdotL / NdotH;
			float VVis = V * VdotH * NdotL / max(NdotH, 1e-8f);
			float Fc = pow(1.0f - VdotH, 5.0f);

			acc.x += (1 - Fc) * VVis;
			acc.y += Fc * VVis;
		}
	}

	return acc / float(count);
}

// Referenced glTF-Sample-Viewer ibl_filtering.frag LUT().
float3 LUT(float NdotV, float roughness)
{
    // Compute spherical view vector: (sin(phi), 0, cos(phi))
    float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

    // The macro surface normal just points up.
	float3 N = float3(0.0f, 0.0f, 1.0f);

    // To make the LUT independant from the material's F0, which is part of the Fresnel term
    // when substituted by Schlick's approximation, we factor it out of the integral,
    // yielding to the form: F0 * I1 + I2
    // I1 and I2 are slighlty different in the Fresnel term, but both only depend on
    // NoL and roughness, so they are both numerically integrated and written into two channels.
	float A = 0.0f;
	float B = 0.0f;
	float C = 0.0f;

	float alpha = roughness * roughness;

	const int sampleCount = 1024;

	for (int i = 0; i < sampleCount; i++)
	{
        // Importance sampling, depending on the distribution.
		float4 importanceSample = GetImportanceSample(i, sampleCount, N, roughness);
		float3 H = importanceSample.xyz;
		// float pdf = importanceSample.w;
		float3 L = normalize(reflect(-V, H));

		float NdotL = saturate(L.z);
		if (NdotL > 0.0f)
		{
			// LUT for GGX distribution.

			// Taken from: https://bruop.github.io/ibl
			// Shadertoy: https://www.shadertoy.com/view/3lXXDB
			// Terms besides V are from the GGX PDF we're dividing by.
			float NdotH = saturate(H.z);
			float VdotH = saturate(dot(V, H));
			float V_pdf = V_GGX(NdotL, NdotV, alpha) * VdotH * NdotL / NdotH;
			float Fc = pow(1.0f - VdotH, 5.0f);
			A += (1.0f - Fc) * V_pdf;
			B += Fc * V_pdf;
			C += 0.0f;
		}
	}

    // The PDF is simply pdf(v, h) -> NDF * <nh>.
    // To parametrize the PDF over l, use the Jacobian transform, yielding to: pdf(v, l) -> NDF * <nh> / 4<vh>
    // Since the BRDF divide through the PDF to be normalized, the 4 can be pulled out of the integral.
	return float3(4.0f * A, 4.0f * B, 4.0f * 2.0f * F_PI * C) / float(sampleCount);
}

float4 main(const VSOutput input) : SV_TARGET
{
	float NdotV = input.TexCoord.x;
	float roughness = input.TexCoord.y;
#if 0
	float3 output = IntegrateDFG_Only(NdotV, roughness); 
#else
	float3 output = LUT(NdotV, roughness);
#endif
	return float4(output, 1.0f);
}