#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"DENY_VERTEX_SHADER_ROOT_ACCESS"\
" | DENY_PIXEL_SHADER_ROOT_ACCESS"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
" | DENY_AMPLIFICATION_SHADER_ROOT_ACCESS"\
" | DENY_MESH_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_ALL)"\
", DescriptorTable(UAV(u0), visibility = SHADER_VISIBILITY_ALL)"\

struct RootConstants
{
	uint MeshletCount;
};
ConstantBuffer<RootConstants> CbRootConst : register(b0);

struct ClasMeshData
{
	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;
    uint clusterCount;
    uint vertexBufferStride;
    uint indexBufferStride;
    uint positionTruncateBits;
};
StructuredBuffer<ClasMeshData> ClasMeshDataSB : register(t0);

struct ClasInputData
{
	uint clusterID;
	uint triangleCount;
	uint vertexCount;
	uint vertexBufferOffset;
	uint indexBufferOffset;
    uint meshIdx;
};
StructuredBuffer<ClasInputData> ClasInputDataSB : register(t1);

struct MeshletMeshMaterial
{
	uint MeshIdx;
	uint MaterialIdx;
	uint LocalMeshletIdx;
	uint bMasked;
};
StructuredBuffer<MeshletMeshMaterial> SbMeshletMeshMaterialTable : register(t2);

// D3D12 SDKの同名構造体と同じレイアウト
//TODO: ByteAddressBufferにしないとうまくいかない？
struct D3D12_RTAS_OPERATION_BUILD_CLAS_FROM_TRIANGLES_ARGS
{
    uint ClusterID;
    uint ClusterFlags;
    uint16_t TriangleCount;
    uint16_t UniqueVertexIndexCount;
    uint BaseGeometryIndexAndFlags;
    uint OpacityMicromapBaseLocation;
    uint16_t VertexBufferStride;
    uint16_t IndexBufferStride;
    uint16_t OpacityMicromapIndexBufferStride;
    uint16_t GeometryIndexAndFlagsArrayStride;
    uint16_t PositionTruncateBitCount;
    uint16_t ReservedPadding;
    uint64_t VertexBuffer;
    uint64_t IndexBuffer;
    uint64_t GeometryIndexAndFlagsArray;
    uint64_t GeometryIndexAndFlagsIndexBuffer;
    uint64_t OpacityMicromapArray;
    uint64_t OpacityMicromapIndexBuffer;
};

RWStructuredBuffer<D3D12_RTAS_OPERATION_BUILD_CLAS_FROM_TRIANGLES_ARGS> RWBuildCLASArgsSB : register(u0);

[RootSignature(ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint meshletIdx = dtid.x;
	if (meshletIdx >= CbRootConst.MeshletCount)
	{
		return;
	}

	MeshletMeshMaterial meshMaterial = SbMeshletMeshMaterialTable[meshletIdx];
	uint meshIdx = meshMaterial.MeshIdx;
	uint matIdx = meshMaterial.MaterialIdx;
	uint localMeshletIdx = meshMaterial.LocalMeshletIdx;

    ClasMeshData clasMeshData = ClasMeshDataSB[meshIdx];

	RWBuildCLASArgsSB[meshletIdx].ClusterID = ClasInputDataSB[meshletIdx].clusterID;
    RWBuildCLASArgsSB[meshletIdx].ClusterFlags = 0;
    RWBuildCLASArgsSB[meshletIdx].TriangleCount = (uint16_t)ClasInputDataSB[meshletIdx].triangleCount;
    RWBuildCLASArgsSB[meshletIdx].UniqueVertexIndexCount = (uint16_t)ClasInputDataSB[meshletIdx].vertexCount;
    RWBuildCLASArgsSB[meshletIdx].BaseGeometryIndexAndFlags =
        (ClasInputDataSB[meshletIdx].meshIdx & 0x00FFFFFF)
        | 0x80000000; // D3D12_RTAS_CLUSTERED_GEOMETRY_FLAG_OPAQUE
    RWBuildCLASArgsSB[meshletIdx].OpacityMicromapBaseLocation = 0;
    RWBuildCLASArgsSB[meshletIdx].VertexBufferStride = (uint16_t)clasMeshData.vertexBufferStride;
    RWBuildCLASArgsSB[meshletIdx].IndexBufferStride = (uint16_t)clasMeshData.indexBufferStride;
    RWBuildCLASArgsSB[meshletIdx].OpacityMicromapIndexBufferStride = 0;
    RWBuildCLASArgsSB[meshletIdx].GeometryIndexAndFlagsArrayStride = 0;
    RWBuildCLASArgsSB[meshletIdx].PositionTruncateBitCount = (uint16_t)clasMeshData.positionTruncateBits;
    RWBuildCLASArgsSB[meshletIdx].VertexBuffer = clasMeshData.vertexBufferAddress + ClasInputDataSB[meshletIdx].vertexBufferOffset;
    RWBuildCLASArgsSB[meshletIdx].IndexBuffer = clasMeshData.indexBufferAddress + ClasInputDataSB[meshletIdx].indexBufferOffset;
    RWBuildCLASArgsSB[meshletIdx].GeometryIndexAndFlagsArray = 0;
    RWBuildCLASArgsSB[meshletIdx].GeometryIndexAndFlagsIndexBuffer = 0;
    RWBuildCLASArgsSB[meshletIdx].OpacityMicromapArray = 0;
    RWBuildCLASArgsSB[meshletIdx].OpacityMicromapIndexBuffer = 0;
}