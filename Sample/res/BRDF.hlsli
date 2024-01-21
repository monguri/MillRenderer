#ifndef BRDF_HLSLI
#define BRDF_HLSLI

#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

//
// Implementation is based on glTF2.0 BRDF sample implementation.
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation
//
float3 SchlickFresnel(float3 f0, float3 f90, float VH)
{
	return f0 + (f90 - f0) * pow(saturate(1.0f - VH), 5.0f);
}

float D_GGX(float NH, float alpha)
{
    float a = NH * alpha;
    float k = alpha / (1.0f - NH * NH + a * a);
    return k * k * (1.0f / F_PI);
}

float V_SmithGGXCorrelated(float NL, float NV, float alpha)
{
	float a2 = alpha * alpha;
    float GGXV = NL * sqrt(NV * NV * (1.0f - a2) + a2);
    float GGXL = NV * sqrt(NL * NL * (1.0f - a2) + a2);
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
	float D = D_GGX(alpha, NdotH);
	float V = V_SmithGGXCorrelated(NdotL, NdotV, alpha);
	float3 specularTerm = D * V;

	float3 F = SchlickFresnel(f0, f90, VdotH);

	return NdotL * lerp(diffuseTerm, specularTerm, F);
}

#endif // BRDF_HLSLI
