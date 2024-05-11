struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbDownsample : register(b0)
{
	int SrcWidth : packoffset(c0);
	int SrcHeight : packoffset(c0.y);
}

Texture2D SrcColorMap : register(t0);
SamplerState LinearClampMipPointSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 InvExtent = float2(1.0f / SrcWidth, 1.0f / SrcHeight);

	float2 UVs[4];
	UVs[0] = input.TexCoord + float2(-1, -1) * InvExtent;
	UVs[1] = input.TexCoord + float2(1, -1) * InvExtent;
	UVs[2] = input.TexCoord + float2(-1, 1) * InvExtent;
	UVs[3] = input.TexCoord + float2(1, 1) * InvExtent;

	float4 samples[4];
	for (uint i = 0; i < 4; i++)
	{
		samples[i] = SrcColorMap.Sample(LinearClampMipPointSmp, UVs[i]);
	}

	return (samples[0] + samples[1] + samples[2] + samples[3]) * 0.25f;
}