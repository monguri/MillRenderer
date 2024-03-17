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
	float AlphaCutoff;
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
Texture2D DFGMap : register(t3);
TextureCube DiffuseLDMap : register(t4);
TextureCube SpecularLDMap : register(t5);

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
	float3 R = normalize(reflect(V, N));
	float NV = saturate(dot(N, V));

	float3 Kd = baseColor.rgb * (1.0f - metallic);
	float3 Ks = baseColor.rgb * metallic;

	float3 lit = 0;
	lit += EvaluateIBLDiffuse(N) * Kd;
	lit += EvaluateIBLSpecular(NV, N, R, Ks, roughness, TextureSize, MipCount);
	output.Color.rgb = lit * LightIntensity;
	output.Color.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;
	return output;
}

