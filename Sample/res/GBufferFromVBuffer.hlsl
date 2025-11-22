#include "ShadowMap.hlsli"

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
", StaticSampler"\
"("\
"s1"\
", filter = FILTER_ANISOTROPIC"\
", addressU = TEXTURE_ADDRESS_WRAP"\
", addressV = TEXTURE_ADDRESS_WRAP"\
", addressW = TEXTURE_ADDRESS_WRAP"\
", maxAnisotropy = 16"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\
", StaticSampler"\
"("\
"s2"\
", filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_LESS_EQUAL"\
", borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"

// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;
static const uint NUM_SPOT_LIGHTS = 3;

struct CbDrawGBufferDescHeapIndices
{
	uint CbTransform[MAX_MESH_COUNT];
	uint CbMesh[MAX_MESH_COUNT];
	uint SbVertexBuffer[MAX_MESH_COUNT];
	uint SbIndexBuffer[MAX_MESH_COUNT];
	uint CbMaterial[MAX_MESH_COUNT];
	uint BaseColorMap[MAX_MESH_COUNT];
	uint MetallicRoughnessMap[MAX_MESH_COUNT];
	uint NormalMap[MAX_MESH_COUNT];
	uint EmissiveMap[MAX_MESH_COUNT];
	uint AOMap[MAX_MESH_COUNT];

	uint CbCamera;
	uint VBuffer;
	uint DepthBuffer;
	uint CbGBufferFromVBuffer;

	// Sponza用
	uint DirLightShadowMap;
	uint SpotLightShadowMap[NUM_SPOT_LIGHTS];

	// IBL用
	uint CbIBL;
	uint DFGMap;
	uint DiffuseLDMap;
	uint SpecularLDMap;
};

struct GBufferFromVBuffer
{
	float4x4 InvProjMatrix;
	float Near;
};

ConstantBuffer<CbDrawGBufferDescHeapIndices> CbDescHeapIndices : register(b0);

SamplerState PointClampSmp : register(s0);
SamplerState AnisotropicWrapSmp : register(s1);
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s2);
#else
SamplerState ShadowSmp : register(s2);
#endif

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

struct PSOutput
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
};

// C++側の定義と値の一致が必要
static const float INVALID_VISIBILITY = 0xffffffff;

// https://shikihuiku.github.io/post/projection_matrix/
// deviceZ = -Near / viewZ
// Nearは0.1mくらいにするので、viewZを100kmまで対応しても安全な値にした
#ifndef DEVICE_Z_MIN_VALUE
#define DEVICE_Z_MIN_VALUE 1e-7f
#endif //DEVICE_Z_FURTHEST

// TODO: いろんなSSパスで冗長
float ConvertFromDeviceZtoViewZ(float deviceZ, float near)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -near / max(deviceZ, DEVICE_Z_MIN_VALUE);
}

float3 ConverFromNDCToVS(float4 ndcPos, float near, float4x4 invProjMat)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float viewPosZ = ConvertFromDeviceZtoViewZ(deviceZ, near);
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 viewPos = mul(invProjMat, clipPos);
	
	return viewPos.xyz;
}

[RootSignature(ROOT_SIGNATURE)]
PSOutput main(VSOutput input)
{
	Texture2D<uint2> VBuffer = ResourceDescriptorHeap[CbDescHeapIndices.VBuffer];
	uint2 visibility = VBuffer.Sample(PointClampSmp, input.TexCoord);
	uint triangleIdx = visibility.y;
	// visibilityの初期値はINVALID_VISIBILITY。xとyどちらをチェックしてもいいがとりあえずyでチェック
	if (triangleIdx == INVALID_VISIBILITY)
	{
		discard;
	}

	uint materialId = visibility.x >> 16;
	uint meshIdx = visibility.x & 0xffff;

	Texture2D DepthBuffer = ResourceDescriptorHeap[CbDescHeapIndices.DepthBuffer];

	float deviceZ = DepthBuffer.Sample(PointClampSmp, input.TexCoord).r;
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	//TODO: input.TexCoordは+0.5の必要はある？

	ConstantBuffer<GBufferFromVBuffer> CbGBufferFromVBuffer = ResourceDescriptorHeap[CbDescHeapIndices.CbGBufferFromVBuffer];

	PSOutput output = (PSOutput)0;
	return output;
}