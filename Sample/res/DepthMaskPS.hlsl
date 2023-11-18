struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 WorldPos : WORLD_POS;
	float3x3 InvTangentBasis : INV_TANGENT_BASIS;
};

cbuffer CbMaterial : register(b7)
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float AlphaCutoff;
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
