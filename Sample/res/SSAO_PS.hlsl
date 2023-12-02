struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

float4 main() : SV_TARGET0
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}