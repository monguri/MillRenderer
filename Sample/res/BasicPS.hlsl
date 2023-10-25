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
};

cbuffer CbLight : register(b1)
{
	float TextureSize : packoffset(c0);
	float MipCount : packoffset(c0.y);
	float LightIntensity : packoffset(c0.z);
	float3 LightDirection : packoffset(c1); // TODO:使ってない
};

cbuffer CbCamera : register(b2)
{
	float3 CameraPosition : packoffset(c0);
};

Texture2D DFGMap : register(t0);
SamplerState DFGSmp : register(s0);

TextureCube DiffuseLDMap : register(t1);
SamplerState DiffuseLDSmp : register(s1);

TextureCube SpecularLDMap : register(t2);
SamplerState SpecularLDSmp : register(s2);

Texture2D BaseColorMap : register(t3);
SamplerState BaseColorSmp : register(s3);

Texture2D MetallicMap : register(t4);
SamplerState MetallicSmp : register(s4);

Texture2D RoughnessMap : register(t5);
SamplerState RoughnessSmp : register(s5);

Texture2D NormalMap : register(t6);
SamplerState NormalSmp : register(s6);

float3 GetSpecularDominantDir(float3 N, float3 R, float roughness)
{
	float smoothness = saturate(1.0f - roughness);
	float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(N, R, lerpFactor);
}

float3 EvaluateIBLDiffuse(float3 N)
{
	return DiffuseLDMap.Sample(DiffuseLDSmp, N).rgb;
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
	float3 preLD = SpecularLDMap.SampleLevel(SpecularLDSmp, dominantR, mipLevel).xyz;

    // 事前積分したDFGをサンプルする.
    // Fc = ( 1 - HdotL )^5
    // PreIntegratedDFG.r = Gvis * (1 - Fc)
    // PreIntegratedDFG.g = Gvis * Fc
	float2 preDFG = DFGMap.SampleLevel(DFGSmp, float2(NdotV, roughness), 0).xy;

    // LD * (f0 * Gvis * (1 - Fc) + Gvis * Fc)
	return preLD * (f0 * preDFG.x + preDFG.y);
}

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;

	float3 V = normalize(input.WorldPos.xyz - CameraPosition);
	float3 N = NormalMap.Sample(NormalSmp, input.TexCoord).xyz * 2.0f - 1.0f;
	N = mul(input.InvTangentBasis, N);
	float3 R = normalize(reflect(V, N));

	float NV = saturate(dot(N, V));

	float3 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord).rgb;
	float metallic = MetallicMap.Sample(MetallicSmp, input.TexCoord).r;
	float roughness = RoughnessMap.Sample(RoughnessSmp, input.TexCoord).r;

	float3 Kd = baseColor * (1.0f - metallic);
	float3 Ks = baseColor * metallic;

	float3 lit = 0;
	lit += EvaluateIBLDiffuse(N) * Kd;
	lit += EvaluateIBLSpecular(NV, N, R, Ks, roughness, TextureSize, MipCount);

	output.Color.rgb = lit * LightIntensity;
	output.Color.a = 1.0f;

	return output;
}
