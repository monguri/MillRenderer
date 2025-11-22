#include "ShadowMap.hlsli"

// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;
static const uint NUM_SPOT_LIGHTS = 3;

struct DrawGBufferDescHeapIndices
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
	uint SbVBuffer;

	// Sponza用
	uint DirLightShadowMap;
	uint SpotLightShadowMap[NUM_SPOT_LIGHTS];

	// IBL用
	uint CbIBL;
	uint DFGMap;
	uint DiffuseLDMap;
	uint SpecularLDMap;
};

ConstantBuffer<DrawGBufferDescHeapIndices> CbDescHeapIndices : register(b0);

Texture2D VisibilityBuffer : register(t0);

SamplerState PointClampSmp : register(s0);
SamplerState AnisotropicWrapSmp : register(s0);
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
SamplerComparisonState ShadowSmp : register(s1);
#else
SamplerState ShadowSmp : register(s1);
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

PSOutput main(VSOutput input)
{
	PSOutput output = (PSOutput)0;
	return output;
}