#include "BRDF.hlsli"

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
};

cbuffer CbCamera : register(b0)
{
	float3 CameraPosition : packoffset(c0);
};

cbuffer CbMaterial : register(b1)
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
};

cbuffer CbIBL : register(b2)
{
	float TextureSize : packoffset(c0);
	float MipCount : packoffset(c0.y);
	float LightIntensity : packoffset(c0.z);
};

Texture2D BaseColorMap : register(t0);
Texture2D MetallicRoughnessMap : register(t1);
Texture2D NormalMap : register(t2);
Texture2D EmissiveMap : register(t3);
Texture2D AOMap : register(t4);
Texture2D DFGMap : register(t5);
TextureCube DiffuseLDMap : register(t6);
TextureCube SpecularLDMap : register(t7);

SamplerState AnisotropicWrapSmp : register(s0);
SamplerState LinearWrapSmp : register(s1);

float3 GetSpecularDominantDir(float3 N, float3 R, float roughness)
{
	float smoothness = saturate(1.0f - roughness);
	float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(N, R, lerpFactor);
}

float3 EvaluateIBLDiffuse(float3 N)
{
	return DiffuseLDMap.Sample(LinearWrapSmp, N).rgb;
}

// Referenced glTF-Sample-Viewer ibl.glsl
float3 GetIBLRadianceLambertian(float3 N, float3 NdotV, float roughness, float3 diffuseColor, float3 F0, float3 Fr, float2 f_ab)
{
	float3 irradiance = DiffuseLDMap.Sample(LinearWrapSmp, N).rgb;

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera

	float3 k_S = F0 + Fr * pow(1.0f - NdotV, 5.0f);
	float3 FssEss = k_S * f_ab.x + f_ab.y; // <--- GGX / specular light contribution (scale it down if the specularWeight is low)

    // Multiple scattering, from Fdez-Aguera
	float Ems = (1.0f - (f_ab.x + f_ab.y));
	float3 F_avg = (F0 + (1.0f - F0) / 21.0f);
	float3 FmsEms = Ems * FssEss * F_avg / (1.0f - F_avg * Ems);
	float3 k_D = diffuseColor * (1.0f - FssEss + FmsEms); // we use +FmsEms as indicated by the formula in the blog post (might be a typo in the implementation)

	return (FmsEms + k_D) * irradiance;
}

float RoughnessToMipLevel(float linearRoughness, float mipCount)
{
	return (mipCount - 1) * linearRoughness;
}

float3 EvaluateIBLSpecular
(
	float NdotV,
	float3 N,
	float3 R,
	float3 f0,
	float roughness,
	float textureSize,
	float mipCount
)
{
	float a = roughness * roughness;
	float3 dominantR = GetSpecularDominantDir(N, R, a);

    // 関数を再構築.
    // L * D * (f0 * Gvis * (1 - Fc) + Gvis * Fc) * cosTheta / (4 * NdotL * NdotV).
	NdotV = max(NdotV, 0.5f / textureSize);
	float mipLevel = RoughnessToMipLevel(roughness, mipCount);
	float3 preLD = SpecularLDMap.SampleLevel(LinearWrapSmp, dominantR, mipLevel).xyz;

    // 事前積分したDFGをサンプルする.
    // Fc = ( 1 - HdotL )^5
    // PreIntegratedDFG.r = Gvis * (1 - Fc)
    // PreIntegratedDFG.g = Gvis * Fc
	float2 preDFG = DFGMap.SampleLevel(LinearWrapSmp, float2(NdotV, roughness), 0).xy;

    // LD * (f0 * Gvis * (1 - Fc) + Gvis * Fc)
	return preLD * (f0 * preDFG.x + preDFG.y);
}

// Referenced glTF-Sample-Viewer ibl.glsl
float3 GetIBLRadianceGGX(float3 N, float3 R, float3 NdotV, float roughness, float3 F0, float3 Fr, float2 f_ab, float mipCount)
{
	// TODO: float3 dominantR = GetSpecularDominantDir(N, R, a);なし
	float mipLevel = RoughnessToMipLevel(roughness, mipCount);

	// TODO: NdotV = max(NdotV, 0.5f / textureSize);なし
	// glTF-Sample-Viewer is not using GetSpecularDominantDir()
	float3 specularLight = SpecularLDMap.SampleLevel(LinearWrapSmp, R, mipLevel).xyz;
	
    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
	float3 k_S = F0 + Fr * pow(1.0f - NdotV, 5.0f);
	float3 FssEss = k_S * f_ab.x + f_ab.y;

	return specularLight * FssEss;
}

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;

	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
#ifdef ALPHA_MODE_MASK
	if (baseColor.a < AlphaCutoff)
	{
		discard;
	}
#endif

	baseColor.rgb *= BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.Sample(AnisotropicWrapSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * MetallicFactor;
	float roughness = metallicRoughness.y * RoughnessFactor;

	float3 N = NormalMap.Sample(AnisotropicWrapSmp, input.TexCoord).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(input.InvTangentBasis, N);
	float3 V = normalize(CameraPosition - input.WorldPos);
	float3 R = normalize(reflect(-V, N));
	float NdotV = saturate(dot(N, V));

#if 0
	float3 Kd = baseColor.rgb * (1.0f - metallic);
	float3 Ks = baseColor.rgb * metallic;

	float3 lit = 0;
	lit += EvaluateIBLDiffuse(N) * Kd;
	lit += EvaluateIBLSpecular(NdotV, N, R, Ks, roughness, TextureSize, MipCount);
#else

	float3 cDiff = lerp(baseColor.rgb, 0.0f, metallic);
	float3 F0 = ComputeF0(baseColor.rgb, metallic);
	float3 Fr = max(1.0f - roughness, F0) - F0;
	
	float2 f_ab = DFGMap.SampleLevel(LinearWrapSmp, float2(NdotV, roughness), 0).xy;

	float3 diffuse = GetIBLRadianceLambertian(N, NdotV, roughness, cDiff, F0, Fr, f_ab);
	float3 specular = GetIBLRadianceGGX(N, R, NdotV, roughness, F0, Fr, f_ab, MipCount);
	float3 lit = diffuse + specular;
#endif

	float3 emissive = 0;
	if (bExistEmissiveTex)
	{
		emissive = EmissiveFactor;
		emissive *= EmissiveMap.Sample(AnisotropicWrapSmp, input.TexCoord).rgb;
	}

	float AO = 1;
	if (bExistAOTex)
	{
		AO = AOMap.Sample(AnisotropicWrapSmp, input.TexCoord).r;
	}

	output.Color.rgb = lit * LightIntensity * AO + emissive;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;
	return output;
}

