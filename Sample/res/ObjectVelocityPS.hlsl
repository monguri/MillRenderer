struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 CurClipPos : CUR_CLIP_POSITION;
	float2 PrevClipPos : PREV_CLIP_POSITION;
};

float4 main(const VSOutput input) : SV_TARGET0
{
	float2 curUV = (input.CurClipPos + float2(1, -1)) * float2(0.5, -0.5);
	float2 prevUV = (input.PrevClipPos + float2(1, -1)) * float2(0.5, -0.5);
	return float4(curUV - prevUV, 0, 1);
}