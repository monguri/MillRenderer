struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 CurClipPos : CUR_CLIP_POSITION;
	float2 PrevClipPos : PREV_CLIP_POSITION;
};

cbuffer CbTransform : register(b0)
{
	float4x4 CurWVP : packoffset(c0);
	float4x4 PrevWVP : packoffset(c4);
}

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float4 localPos = float4(input.Position, 1.0f);
	output.Position = mul(CurWVP, localPos);
	output.CurClipPos = output.Position.xy;
	output.PrevClipPos = mul(PrevWVP, localPos).xy;

	return output;
}