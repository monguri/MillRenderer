struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 Tangent : TANGENT;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
	float3 DirLightShadowCoord : TEXCOORD2;
};

cbuffer CbTransform : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
	float4x4 ModelToDirLightShadowMap : packoffset(c4);
}

cbuffer CbMesh : register(b1)
{
	float4x4 World : packoffset(c0);
}

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float4 localPos = float4(input.Position, 1.0f);
	float4 worldPos = mul(World, localPos);
	float4 projPos = mul(ViewProj, worldPos);

	output.Position = projPos;
	output.TexCoord = input.TexCoord;
	output.WorldPos = worldPos.xyz;

	float4 dirLightShadowPos = mul(ModelToDirLightShadowMap, localPos);
	// dividing by w is not necessary because it is 1 by orthogonal.
	output.DirLightShadowCoord = dirLightShadowPos.xyz / dirLightShadowPos.w;

	float3 N = normalize(mul((float3x3)World, input.Normal));
	float3 T = normalize(mul((float3x3)World, input.Tangent));
	float3 B = normalize(cross(N, T));

	output.InvTangentBasis = transpose(float3x3(T, B, N));

	return output;
}