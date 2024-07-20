#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbCameraVelocity : register(b0)
{
	float4x4 ClipToPrevClip : packoffset(c0);
}

Texture2D DepthMap : register(t0);
Texture2D ObjectVelocityMap : register(t1);
SamplerState PointClampSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float2 objectVelocity = ObjectVelocityMap.Sample(PointClampSmp, input.TexCoord).rg;
	if (abs(objectVelocity.r) + abs(objectVelocity.g) > 0.0f) // TODO: should consider error?
	{
		return float4(objectVelocity, 0, 1);
	}

	// [0, 1] to [-1, 1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);

	float deviceZ = DepthMap.Sample(PointClampSmp, input.TexCoord).r;

	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float4 prevClipPos = mul(ClipToPrevClip, ndcPos);
	float4 prevNdcPos = prevClipPos / prevClipPos.w;
	float2 prevUV = (prevNdcPos.xy + float2(1, -1)) * float2(0.5, -0.5);

	// Even when ClipToPrevClip should be identity, it includes float precision error.
	// So if velocity is too small, correct to zero for precision of paths after.
	// ClipToPrevClip includes about 1e-6f size error.
	float2 velocityCorrected = input.TexCoord - prevUV;
	if (abs(velocityCorrected.x) < 1e-5f)
	{
		velocityCorrected.x = 0.0f;
	}
	if (abs(velocityCorrected.y) < 1e-5f)
	{
		velocityCorrected.y = 0.0f;
	}

	return float4(velocityCorrected, 0, 1);
}