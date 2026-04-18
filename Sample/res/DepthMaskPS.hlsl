struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
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

ConstantBuffer<Material> CbMaterial : register(b0);
Texture2D BaseColorMap : register(t0);
SamplerState BaseColorSmp : register(s0);

void main(VSOutput input)
{
	float4 baseColor = BaseColorMap.Sample(BaseColorSmp, input.TexCoord);
	if (baseColor.a < CbMaterial.AlphaCutoff)
	{
		discard;
	}
}
