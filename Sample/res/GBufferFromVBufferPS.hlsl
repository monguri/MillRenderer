#include "BRDF.hlsli"

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
", DescriptorTable(CBV(b1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(CBV(b2), visibility = SHADER_VISIBILITY_PIXEL)"\
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

// C++側の定義と値の一致が必要
static const uint MAX_MESH_COUNT = 256;

static const uint EACH_MESH_DESCRIPTOR_COUNT = 6;

struct MeshesDescHeapIndices
{
	//uint CbMesh[MAX_MESH_COUNT];
	//uint SbVertexBuffer[MAX_MESH_COUNT];
	//uint SbMeshletBuffer[MAX_MESH_COUNT];
	//uint SbMeshletVerticesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletTrianglesBuffer[MAX_MESH_COUNT];
	//uint SbMeshletAABBInfosBuffer[MAX_MESH_COUNT];

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[MAX_MESH_COUNT * EACH_MESH_DESCRIPTOR_COUNT / 4];
};

static const uint CbMeshBaseIdx = 0;
static const uint SbVertexBufferBaseIdx = CbMeshBaseIdx  + MAX_MESH_COUNT;
static const uint SbMeshletBufferBaseIdx = SbVertexBufferBaseIdx + MAX_MESH_COUNT;
static const uint SbMeshletVerticesBufferBaseIdx = SbMeshletBufferBaseIdx + MAX_MESH_COUNT;
static const uint SbMeshletTrianglesBufferBaseIdx = SbMeshletVerticesBufferBaseIdx + MAX_MESH_COUNT;

static const uint MAX_MATERIAL_COUNT = 256;

static const uint EACH_MATERIAL_DESCRIPTOR_COUNT = 6;

struct MaterialsDescHeapIndices
{
	//uint CbMaterial[MAX_MATERIAL_COUNT];
	//uint BaseColorMap[MAX_MATERIAL_COUNT];
	//uint MetallicRoughnessMap[MAX_MATERIAL_COUNT];
	//uint NormalMap[MAX_MATERIAL_COUNT];
	//uint EmissiveMap[MAX_MATERIAL_COUNT];
	//uint AOMap[MAX_MATERIAL_COUNT];

	//TODO: 配列変数が複数あるとメインメモリとのメモリマッピングがうまくいかないので
	// ひとつのuint[]にまとめてインデックスは別途ゲッターを用意する
	uint4 Indices[MAX_MATERIAL_COUNT * EACH_MATERIAL_DESCRIPTOR_COUNT / 4];
};

static const uint CbMaterialBaseIdx = 0;
static const uint BaseColorMapBaseIdx = CbMaterialBaseIdx + MAX_MATERIAL_COUNT;
static const uint MetallicRoughnessMapBaseIdx = BaseColorMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint NormalMapBaseIdx = MetallicRoughnessMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint EmissiveMapBaseIdx = NormalMapBaseIdx + MAX_MATERIAL_COUNT;
static const uint AOMapBaseIdx = EmissiveMapBaseIdx + MAX_MATERIAL_COUNT;

// TODO:冗長
// VBの頂点構造体
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
	float2 TexCoord : TEXCOORD;
};

struct PSOutput
{
	float4 BaseColor : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	float2 MetallicRoughness : SV_TARGET2;
	float3 Emissive : SV_TARGET3;
};

struct Mesh
{
	float4x4 World;
	uint bMovable;
};

struct meshopt_Meshlet
{
	uint VertOffset;
	uint TriOffset;
	uint VertCount;
	uint TriCount;
};

struct Camera
{
	float4x4 ViewProj;
	float3 CameraPosition;
	uint DebugViewType;
	float4x4 View;
	float4x4 InvProj;
	float4x4 InvViewProj;
	uint Width;
	uint Height;
	float Near;
	float Padding[1];
};

struct Material
{
	float3 BaseColorFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float3 EmissiveFactor;
	uint bAlphaMask;
	float AlphaCutoff;
	uint bExistEmissiveTex;
	uint bExistAOTex;
};

struct MeshletMeshMaterial
{
	uint MeshIdx;
	uint MaterialIdx;
	uint LocalMeshletIdx;
	uint bMasked;
};

ConstantBuffer<MeshesDescHeapIndices> CbMeshesDescHeapIndices : register(b0);
ConstantBuffer<MaterialsDescHeapIndices> CbMaterialsDescHeapIndices : register(b1);
ConstantBuffer<Camera> CbCamera : register(b2);

Texture2D<uint2> VBuffer : register(t0);
StructuredBuffer<MeshletMeshMaterial> SbMeshletMeshMaterialTable : register(t1);

uint GetMeshDescHeapIndex(uint meshIdx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMeshesDescHeapIndices.Indices[meshIdx >> 2][meshIdx & 0b11];
	//uint ret = CbMeshesDescHeapIndices.Indices[meshIdx / 4][meshIdx % 4];
	return ret;
}

uint GetMaterialDescHeapIndex(uint matIdx)
{
	// [idx / 4][idx % 4]にあたる
	// CBなので4つ分のインデックスをuint4で1セットにしているため
	uint ret = CbMaterialsDescHeapIndices.Indices[matIdx >> 2][matIdx & 0b11];
	//uint ret = CbMaterialsDescHeapIndices.Indices[matIdx / 4][matIdx % 4];
	return ret;
}

SamplerState PointClampSmp : register(s0);
SamplerState AnisotropicWrapSmp : register(s1);

// C++側の定義と値の一致が必要
static const float INVALID_VISIBILITY = 0xffffffff;

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// からコードをとってきた
struct BarycentricDeriv
{
	float3 m_lambda;
	float3 m_ddx;
	float3 m_ddy;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	BarycentricDeriv ret = (BarycentricDeriv)0;

	float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	float ddxSum = dot(ret.m_ddx, float3(1,1,1));
	float ddySum = dot(ret.m_ddy, float3(1,1,1));

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);

	ret.m_lambda.x = interpW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
	ret.m_lambda.y = interpW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
	ret.m_lambda.z = interpW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

	ret.m_ddx *= (2.0f/winSize.x);
	ret.m_ddy *= (2.0f/winSize.y);
	ddxSum    *= (2.0f/winSize.x);
	ddySum    *= (2.0f/winSize.y);

	ret.m_ddy *= -1.0f;
	ddySum    *= -1.0f;

	float interpW_ddx = 1.0f / (interpInvW + ddxSum);
	float interpW_ddy = 1.0f / (interpInvW + ddySum);

	ret.m_ddx = interpW_ddx*(ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
	ret.m_ddy = interpW_ddy*(ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;  

	return ret;
}

// 上記記事のInterpolateWithDeriv()を参考にしている
float3 Baryinterpolate3(BarycentricDeriv deriv, float3 v0, float3 v1, float3 v2)
{
	//TDOO: float3x3にまとめてmul()してもよい
	float3 ret;
	ret.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_lambda);
	ret.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_lambda);
	ret.z = dot(float3(v0.z, v1.z, v2.z), deriv.m_lambda);
	return ret;
}

void BaryInterpolateDeriv2(BarycentricDeriv deriv, float2 v0, float2 v1, float2 v2, out float2 interp, out float2 ddx, out float2 ddy)
{
	interp.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_lambda);
	interp.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_lambda);
	ddx.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_ddx);
	ddx.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_ddx);
	ddy.x = dot(float3(v0.x, v1.x, v2.x), deriv.m_ddy);
	ddy.y = dot(float3(v0.y, v1.y, v2.y), deriv.m_ddy);
}

[RootSignature(ROOT_SIGNATURE)]
PSOutput main(VSOutput input)
{
	uint2 visibility = VBuffer.Sample(PointClampSmp, input.TexCoord);
	// visibility.xの初期値はINVALID_VISIBILITY
	if (visibility.x == INVALID_VISIBILITY)
	{
		discard;
	}

	uint meshletIdx = (visibility.x >> 7) & 0x1ffffff; // 25bit
	uint triangleIdx = visibility.x & 0x7f; // 7bit

	MeshletMeshMaterial meshMaterial = SbMeshletMeshMaterialTable[meshletIdx];
	uint meshIdx = meshMaterial.MeshIdx;
	uint matIdx = meshMaterial.MaterialIdx;
	uint localMeshletIdx = meshMaterial.LocalMeshletIdx;

	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);

	StructuredBuffer<meshopt_Meshlet> meshlets = ResourceDescriptorHeap[GetMeshDescHeapIndex(SbMeshletBufferBaseIdx + meshIdx)];
	meshopt_Meshlet meshlet = meshlets[localMeshletIdx];

	StructuredBuffer<uint> meshletsTriangles = ResourceDescriptorHeap[GetMeshDescHeapIndex(SbMeshletTrianglesBufferBaseIdx + meshIdx)];

	uint triBaseIdx = meshlet.TriOffset + triangleIdx * 3;
	uint index0 = meshletsTriangles[triBaseIdx];
	uint index1 = meshletsTriangles[triBaseIdx + 1];
	uint index2 = meshletsTriangles[triBaseIdx + 2];

	StructuredBuffer<uint> meshletsVertices = ResourceDescriptorHeap[GetMeshDescHeapIndex(SbMeshletVerticesBufferBaseIdx + meshIdx)];
	uint vertIdx0 = meshletsVertices[meshlet.VertOffset + index0];
	uint vertIdx1 = meshletsVertices[meshlet.VertOffset + index1];
	uint vertIdx2 = meshletsVertices[meshlet.VertOffset + index2];

	StructuredBuffer<VSInput> SbVertexBuffer = ResourceDescriptorHeap[GetMeshDescHeapIndex(SbVertexBufferBaseIdx + meshIdx)];
	VSInput vertex0 = SbVertexBuffer[vertIdx0];
	VSInput vertex1 = SbVertexBuffer[vertIdx1];
	VSInput vertex2 = SbVertexBuffer[vertIdx2];

	ConstantBuffer<Mesh> CbMesh = ResourceDescriptorHeap[GetMeshDescHeapIndex(CbMeshBaseIdx + meshIdx)];
	// TODO: 思うに、Triangleの3点がわかるならddx(uv)、ddy(uv)、すなわちDuvDpx、DuvDpyは求まるのでは？ピクセル座標で3頂点のUVからヤコビ案計算でわかりそうなものだ
	// 方法こそ違えど、CalcFullBaryでやっていることと同じでは？
#if 0
	// View Spaceで計算する

	Texture2D DepthBuffer = ResourceDescriptorHeap[CbMeshesDescHeapIndices.DepthBuffer];
	float deviceZ = DepthBuffer.Sample(PointClampSmp, input.TexCoord).r;
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 viewPos = ConverFromNDCToVS(ndcPos, CbCamera.Near, CbCamera.InvProj);


	float3 vsPos0 = mul(CbCamera.View, mul(CbMesh.World, float4(vertex0.Position, 1.0f))).xyz;
	float3 vsPos1 = mul(CbCamera.View, mul(CbMesh.World, float4(vertex1.Position, 1.0f))).xyz;
	float3 vsPos2 = mul(CbCamera.View, mul(CbMesh.World, float4(vertex2.Position, 1.0f))).xyz;

	float3 triNormal = normalize(cross(vsPos1 - vsPos0, vsPos2 - vsPos0));

	float3 cameraPos = float3(0, 0, 0);
	float3 rayDirection = normalize(viewPos - cameraPos);
	float3 planeSurfacePoint = viewPos;
	float3 planeNormal = triNormal;
	float hitT;
	// 必ず衝突するはずなので戻り値は無視
	RayIntersectPlane(float3(0, 0, 0), normalize(viewPos - cameraPos), viewPos, triNormal, hitT);
#endif

	float4 clipPos0 = mul(CbCamera.ViewProj, mul(CbMesh.World, float4(vertex0.Position, 1.0f)));
	float4 clipPos1 = mul(CbCamera.ViewProj, mul(CbMesh.World, float4(vertex1.Position, 1.0f)));
	float4 clipPos2 = mul(CbCamera.ViewProj, mul(CbMesh.World, float4(vertex2.Position, 1.0f)));

	BarycentricDeriv barycentricDeriv = CalcFullBary(clipPos0, clipPos1, clipPos2, screenPos, float2(CbCamera.Width, CbCamera.Height));
	//TODO: SponzaVS.hlslおよびSponzaPS.hlsliの処理と重複するので共通化が必要
	// SponzaVSOutputとSponzaPSOutputを用意して共通関数をhlsliにまとめよう
	// IBL版も同様

	// 頂点出力の各変数の補間
	float3 localPos = Baryinterpolate3(barycentricDeriv, vertex0.Position, vertex1.Position, vertex2.Position);
	float4 worldPos = mul(CbMesh.World, float4(localPos, 1.0f));

	float3 normal = normalize(Baryinterpolate3(barycentricDeriv, vertex0.Normal, vertex1.Normal, vertex2.Normal));
	normal = normalize(mul((float3x3)CbMesh.World, normal));
	float3 tangent = normalize(Baryinterpolate3(barycentricDeriv, vertex0.Tangent, vertex1.Tangent, vertex2.Tangent));
	tangent = normalize(mul((float3x3)CbMesh.World, tangent));
	float3 bitangent = normalize(cross(normal, tangent));
	bitangent = normalize(mul((float3x3)CbMesh.World, bitangent));

	float3x3 invTangentBasis = transpose(float3x3(tangent, bitangent, normal));

	float2 texCoord, texCoordDdx, texCoordDdy;
	BaryInterpolateDeriv2(barycentricDeriv, vertex0.TexCoord, vertex1.TexCoord, vertex2.TexCoord, texCoord, texCoordDdx, texCoordDdy);

	// GBuffer描画に必要なリソースを取得
	ConstantBuffer<Material> CbMaterial = ResourceDescriptorHeap[GetMaterialDescHeapIndex(CbMaterialBaseIdx + matIdx)];
	Texture2D BaseColorMap = ResourceDescriptorHeap[GetMaterialDescHeapIndex(BaseColorMapBaseIdx + matIdx)];
	Texture2D MetallicRoughnessMap = ResourceDescriptorHeap[GetMaterialDescHeapIndex(MetallicRoughnessMapBaseIdx + matIdx)];
	Texture2D NormalMap = ResourceDescriptorHeap[GetMaterialDescHeapIndex(NormalMapBaseIdx + matIdx)];
	Texture2D EmissiveMap = ResourceDescriptorHeap[GetMaterialDescHeapIndex(EmissiveMapBaseIdx + matIdx)];

	PSOutput output = (PSOutput)0;

	//TODO: ここからSponzaPS.hlsliの処理と重複するので共通化が必要
	// SampleGradをしてたり、VSOutput input引数がないなどの違いはある
	float4 baseColor = BaseColorMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy);
	// Maskの分岐は既にVBufferで処理済みなのでここでは不要
	baseColor.rgb *= CbMaterial.BaseColorFactor;

	// metallic value is G. roughness value is B.
	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture
	float2 metallicRoughness = MetallicRoughnessMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).bg;
	float metallic = metallicRoughness.x * CbMaterial.MetallicFactor;
	float roughness = metallicRoughness.y * CbMaterial.RoughnessFactor;

	float3 N = NormalMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).xyz * 2.0f - 1.0f;

	// for GGX specular AA
	N = normalize(N);
	roughness = IsotropicNDFFiltering(N, roughness);

	N = mul(invTangentBasis, N);

	float3 emissive = 0;
	if (CbMaterial.bExistEmissiveTex)
	{
		emissive = CbMaterial.EmissiveFactor;
		emissive *= EmissiveMap.SampleGrad(AnisotropicWrapSmp, texCoord, texCoordDdx, texCoordDdy).rgb;
	}

	switch (CbCamera.DebugViewType)
	{
		case DEBUG_VIEW_TYPE_NONE:
		default:
			output.BaseColor.rgb = baseColor.rgb;
			break;
		case DEBUG_VIEW_TYPE_TRIANGLE_INDEX:
		{
			uint globalTriIndex = triBaseIdx / 3;
			output.BaseColor.rgb = float3
			(
				float((globalTriIndex & 1) + 1) * 0.5f, // (globalTriIndex % 2 + 1) / 2.0
				float((globalTriIndex & 3) + 1) * 0.25f, // (globalTriIndex % 4 + 1) / 4.0
				float((globalTriIndex & 7) + 1) * 0.125f // (globalTriIndex % 8 + 1) / 8.0
			);
		}
			break;
		case DEBUG_VIEW_TYPE_MESHLET_INDEX:
			output.BaseColor.rgb = float3
			(
				float((meshletIdx & 1) + 1) * 0.5f, // (MeshletID % 2 + 1) / 2.0
				float((meshletIdx & 3) + 1) * 0.25f, // (MeshletID % 4 + 1) / 4.0
				float((meshletIdx & 7) + 1) * 0.125f // (MeshletID % 8 + 1) / 8.0
			);
			break;
	}
	output.BaseColor.a = 1.0f;

	output.Normal.xyz = (N + 1.0f) * 0.5f;
	output.Normal.a = 1.0f;

	output.MetallicRoughness.r = metallic;
	output.MetallicRoughness.g = roughness;

	output.Emissive = emissive;
	return output;
}