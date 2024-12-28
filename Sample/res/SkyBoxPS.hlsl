struct VSOutput
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

TextureCube CubeMap : register(t0);
SamplerState CubeSmp : register(s0);

PSOutput main(const VSOutput input)
{
	PSOutput output;

	output.Color = CubeMap.SampleLevel(CubeSmp, input.TexCoord, 0);

	// �@����(0, 0, 0)����
	output.Normal.xyz = 0.5f;
	output.Normal.a = 1;

	// ���˂��N�����Ȃ��悤�Ƀ��^���b�N��0�Aroughness��1
	output.MetallicRoughness.r = 0;
	output.MetallicRoughness.g = 1;

	return output;
}