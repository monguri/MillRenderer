struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
};

cbuffer CbMaterial : register(b1)
{
	float3 BaseColorFactor : packoffset(c0);
	float MetallicFactor : packoffset(c0.w);
	float RoughnessFactor : packoffset(c1);
	float3 EmissiveFactor : packoffset(c1.y);
	float AlphaCutoff : packoffset(c2);
	int bExistEmissiveTex : packoffset(c2.y);
	int bExistAOTex : packoffset(c2.z);
};

Texture2D BaseColorMap : register(t0);
SamplerState BaseColorSmp : register(s0);

void main(VSOutput input)
{
	float4 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord);
	if (baseColor.a < AlphaCutoff)
	{
		discard;
	}
}
