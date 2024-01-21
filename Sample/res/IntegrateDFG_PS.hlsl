#include "BRDF.hlsli"
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

float4 main(const VSOutput input) : SV_TARGET
{
	float NdotV = input.TexCoord.x;
	float roughness = input.TexCoord.y;
	float3 output = IntegrateDFG_Only(NdotV, roughness); 
	return float4(output, 1.0f);
}