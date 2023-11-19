#ifndef BRDF_HLSLI
#define BRDF_HLSLI

#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

float3 SchlickFresnel(float3 specular, float NL)
{
	return specular + (1.0f - specular) * pow((1.0f - NL), 5.0f);
}

float D_GGX(float a, float NH)
{
	float a2 = a * a;
	float f = (NH * a2 - NH) * NH + 1;
	return a2 / (F_PI * f * f);
}

float G2_Smith(float NL, float NV, float a)
{
	float a2 = a * a;
	float NL2 = NL * NL;
	float NV2 = NV * NV;

	float Lambda_V = (-1.0f + sqrt(a2 * (1.0f - NV2) / max(NV2, 1e-8f) + 1.0f)) * 0.5f;
	float Lambda_L = (-1.0f + sqrt(a2 * (1.0f - NL2) / max(NL2, 1e-8f) + 1.0f)) * 0.5f;

	return 1.0f / max(1.0f + Lambda_V + Lambda_L, 1e-8f);
}

float3 ComputeLambert(float3 Kd)
{
	return Kd / F_PI;
}

float3 ComputePhong
(
	float3 Ks,
	float3 shininess,
	float3 LdotR
)
{
	return Ks * ((shininess + 2.0f) / (2.0f * F_PI)) * pow(LdotR, shininess);
}

float3 ComputeGGX_MultiplyNdotL
(
	float3 Ks,
	float roughness,
	float NdotH,
	float NdotV,
	float NdotL
)
{
	float a = roughness * roughness;
	float D = D_GGX(a, NdotH);
	float G = G2_Smith(NdotL, NdotV, a);
	float3 F = SchlickFresnel(Ks, NdotL);

	return (D * G * F) / (4.0f * NdotV); // NdotL becomes 0, so pre-multiply NdotL before the lighting term.
}
#endif // BRDF_HLSLI
