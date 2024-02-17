static const float LINEAR_DEPTH_BIAS = -0.01f;

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
	float4 CurClipPos : CUR_CLIP_POSITION;
	float4 PrevClipPos : PREV_CLIP_POSITION;
};

cbuffer CbObjectVelocity : register(b0)
{
	float4x4 CurWVPWithJitter : packoffset(c0);
	float4x4 CurWVPNoJitter : packoffset(c4);
	float4x4 PrevWVPNoJitter : packoffset(c8);
}

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;

	float4 localPos = float4(input.Position, 1.0f);
	output.Position = mul(CurWVPWithJitter, localPos);
	// to avoid discard by depth test.
	output.Position.z += LINEAR_DEPTH_BIAS;
	output.CurClipPos = mul(CurWVPNoJitter, localPos);
	output.PrevClipPos = mul(PrevWVPNoJitter, localPos);

	return output;
}