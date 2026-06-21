#include "Common.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
" | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
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

struct Camera
{
	float4x4 ViewProj;
	float3 CameraPosition;
	uint DebugViewType;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

ConstantBuffer<Camera> CbCamera : register(b0);
Texture2D<uint2> VBuffer : register(t0);
SamplerState PointClampSmp : register(s0);

[RootSignature(ROOT_SIGNATURE)]
float4 main(VSOutput input) : SV_TARGET
{
	uint visibilityX = VBuffer.Sample(PointClampSmp, input.TexCoord).x;
	if (visibilityX == INVALID_VISIBILITY)
	{
		discard;
	}

	float3 color = 0;
	switch (CbCamera.DebugViewType)
	{
		case DEBUG_VIEW_TYPE_TRIANGLE_INDEX:
		{
			uint primitiveID = visibilityX & 0x7f;
			color = float3
			(
				float((primitiveID & 1) + 1) * 0.5f, // (primitiveID % 2 + 1) / 2.0
				float((primitiveID & 3) + 1) * 0.25f, // (primitiveID % 4 + 1) / 4.0
				float((primitiveID & 7) + 1) * 0.125f // (primitiveID % 8 + 1) / 8.0
			);
		}
			break;
		case DEBUG_VIEW_TYPE_MESHLET_INDEX:
		case DEBUG_VIEW_TYPE_MESHLET_AABB: // AABB‚Ì‚Æ‚«‚ÍMeshletIdx‚à“¯Žž‚É•\Ž¦‚·‚é
		{
			uint meshletIdx = visibilityX >> 7;
			color = float3
			(
				float((meshletIdx & 1) + 1) * 0.5f, // (meshletIdx % 2 + 1) / 2.0
				float((meshletIdx & 3) + 1) * 0.25f, // (meshletIdx % 4 + 1) / 4.0
				float((meshletIdx & 7) + 1) * 0.125f // (meshletIdx % 8 + 1) / 8.0
			);
		}
			break;
	}

	return float4(color, 1.0f);
}