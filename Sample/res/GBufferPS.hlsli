#include "BRDF.hlsli"

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
};

struct PSOutput
{
	float4 BaseColor : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
	float3 Emissive : SV_TARGET3;
};

struct Camera
{
	float4x4 ViewProj;
	float3 CameraPosition;
	uint DebugViewType;
};

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	float AlphaCutoff;
	int bExistEmissiveTex;
	int bExistAOTex;
	uint MaterialID;
};

ConstantBuffer<Camera> CbCamera : register(b0);
ConstantBuffer<Material> CbMaterial : register(b1);

Texture2D BaseColorMap : register(t0);
Texture2D MetallicRoughnessMap : register(t1);
Texture2D NormalMap : register(t2);
Texture2D EmissiveMap : register(t3);

SamplerState AnisotropicWrapSmp : register(s0);

PSOutput main(VSOutput input , uint primitiveID : SV_PrimitiveID)
{
	PSOutput output = (PSOutput)0;

	float4 baseColor = BaseColorMap.Sample(AnisotropicWrapSmp, input.TexCoord);
#ifdef ALPHA_MODE_MASK
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
#endif

	baseColor.rgb *= CbMaterial.BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.Sample(AnisotropicWrapSmp, input.TexCoord).bg;
	float metallic = metallicRoughness.x * CbMaterial.MetallicFactor;
	float roughness = metallicRoughness.y * CbMaterial.RoughnessFactor;

	float3 N = NormalMap.Sample(AnisotropicWrapSmp, input.TexCoord).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(input.InvTangentBasis, N);

	float3 emissive = 0;
	if (CbMaterial.bExistEmissiveTex)
	{
		emissive = EmissiveMap.Sample(AnisotropicWrapSmp, input.TexCoord).rgb;
		emissive *= CbMaterial.EmissiveFactor;
	}

	switch (CbCamera.DebugViewType)
	{
		case DEBUG_VIEW_TYPE_NONE:
		default:
			output.BaseColor.rgb = baseColor.rgb;
			break;
		case DEBUG_VIEW_TYPE_TRIANGLE_INDEX:
		{
			output.BaseColor.rgb = float3
			(
				float((primitiveID & 1) + 1) * 0.5f, // (primitiveID % 2 + 1) / 2.0
				float((primitiveID & 3) + 1) * 0.25f, // (primitiveID % 4 + 1) / 4.0
				float((primitiveID & 7) + 1) * 0.125f // (primitiveID % 8 + 1) / 8.0
			);
		}
			break;
	}

	output.BaseColor.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;

	output.MetallicRoughness.r = metallic;
	output.MetallicRoughness.g = roughness;

	output.Emissive = emissive;
	return output;
}