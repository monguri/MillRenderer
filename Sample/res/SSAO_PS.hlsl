struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D DepthMap : register(t0);
SamplerState DepthSmp : register(s0);

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 result = DepthMap.Sample(DepthSmp, input.TexCoord);
	return result;
}