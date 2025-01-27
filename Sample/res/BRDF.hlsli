#pragma once

#include "Common.hlsli"

// Tokuyoshi, Y., and Kaplanyan, A. S. 2021. Stable Geometric Specular Antialiasing with Projected-Space NDF Filtering. JCGT, 10, 2, 31-58.
// https://cedil.cesa.or.jp/cedil_sessions/view/2395
float IsotropicNDFFiltering(float3 normal, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;

	float SIGMA2 = 0.5f * (1.0f / F_PI);
	float KAPPA = 0.18f;

	float3 dndu = ddx(normal);
	float3 dndv = ddy(normal);

	float kernel = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
	float clampedKernel = min(kernel, KAPPA);

	float filteredAlphaSq = saturate(alphaSq + clampedKernel);
	float filteredRoughness = sqrt(sqrt(filteredAlphaSq));
	return filteredRoughness;
}

//
// Implementation is based on glTF2.0 BRDF sample implementation.
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation
//
float3 SchlickFresnel(float3 f0, float3 f90, float VH)
{
	return f0 + (f90 - f0) * pow(saturate(1.0f - VH), 5.0f);
}

// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float D_GGX(float NdotH, float alphaRoughness)
{
	float alphaRoughnessSq = alphaRoughness * alphaRoughness;
	float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0f) + 1.0f;
	return alphaRoughnessSq / max(F_PI * f * f, 1e-8f);
}

// Smith Joint GGX
// Note: Vis = G / (4 * NdotL * NdotV)
// see Eric Heitz. 2014. Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs. Journal of Computer Graphics Techniques, 3
// see Real-Time Rendering. Page 331 to 336.
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
	float alphaRoughnessSq = alphaRoughness * alphaRoughness;

	float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

	float GGX = GGXV + GGXL;
	if (GGX > 0.0f)
	{
		return 0.5f / GGX;
	}
	else
	{
		return 0.0f;
	}
}

float3 ComputeF0(float3 baseColor, float metallic)
{
	return lerp(0.04f, baseColor, metallic);
}

float3 ComputeBRDF
(
	float3 baseColor,
	float metallic,
	float roughness,
	float VdotH,
	float NdotH,
	float NdotV,
	float NdotL
)
{
	float3 cDiff = lerp(baseColor, 0.0f, metallic);
	float3 f0 = ComputeF0(baseColor, metallic);
	float3 f90 = 1.0f;

	// use lambert for diffuse
	float3 diffuseTerm = cDiff * (1.0f / F_PI);

	float alpha = roughness * roughness;
	float D = D_GGX(NdotH, alpha);
	float V = V_GGX(NdotL, NdotV, alpha);
	float3 specularTerm = D * V;

	float3 F = SchlickFresnel(f0, f90, VdotH);

	return NdotL * lerp(diffuseTerm, specularTerm, F);
}

