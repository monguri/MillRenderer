struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 CurClipPos : CUR_CLIP_POSITION;
	float4 PrevClipPos : PREV_CLIP_POSITION;
};

float4 main(const VSOutput input) : SV_TARGET0
{
	float4 curNDCPos = input.CurClipPos / input.CurClipPos.w;
	float2 curUV = (curNDCPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	float4 prevNDCPos = input.PrevClipPos / input.PrevClipPos.w;
	float2 prevUV = (prevNDCPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	return float4(curUV - prevUV, 0, 1);
}