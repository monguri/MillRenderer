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
", DescriptorTable(UAV(u0), visibility = SHADER_VISIBILITY_ALL)"\

struct ClasMeshData
{
	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;
    uint clusterCount;
    uint vertexBufferStride;
    uint indexBufferStride;
    uint positionTruncateBits;
};
ConstantBuffer<ClasMeshData> ClasMeshDataCB : register(b0);

struct ClasInputData
{
	uint clusterID;
	uint triangleCount;
	uint vertexCount;
	uint vertexBufferOffset;
	uint indexBufferOffset;
    uint meshIdx;
};
StructuredBuffer<ClasInputData> ClasInputDataSB : register(t0);

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
    uint idx = dtid.x;
    if (idx >= ClasMeshDataCB.clusterCount)
    {
        return;
	}

	RWBuildCLASArgsSB[idx].ClusterID = ClasInputDataSB[idx].clusterID;
    RWBuildCLASArgsSB[idx].ClusterFlags = 0;
    RWBuildCLASArgsSB[idx].TriangleCount = (uint16_t)ClasInputDataSB[idx].triangleCount;
    RWBuildCLASArgsSB[idx].UniqueVertexIndexCount = (uint16_t)ClasInputDataSB[idx].vertexCount;
    RWBuildCLASArgsSB[idx].BaseGeometryIndexAndFlags =
        (ClasInputDataSB[idx].meshIdx & 0x00FFFFFF)
        | 0x80000000; // D3D12_RTAS_CLUSTERED_GEOMETRY_FLAG_OPAQUE
    RWBuildCLASArgsSB[idx].OpacityMicromapBaseLocation = 0;
    RWBuildCLASArgsSB[idx].VertexBufferStride = (uint16_t)ClasMeshDataCB.vertexBufferStride;
    RWBuildCLASArgsSB[idx].IndexBufferStride = (uint16_t)ClasMeshDataCB.indexBufferStride;
    RWBuildCLASArgsSB[idx].OpacityMicromapIndexBufferStride = 0;
    RWBuildCLASArgsSB[idx].GeometryIndexAndFlagsArrayStride = 0;
    RWBuildCLASArgsSB[idx].PositionTruncateBitCount = (uint16_t)ClasMeshDataCB.positionTruncateBits;
    RWBuildCLASArgsSB[idx].VertexBuffer = ClasMeshDataCB.vertexBufferAddress + ClasInputDataSB[idx].vertexBufferOffset;
    RWBuildCLASArgsSB[idx].IndexBuffer = ClasMeshDataCB.indexBufferAddress + ClasInputDataSB[idx].indexBufferOffset;
    RWBuildCLASArgsSB[idx].GeometryIndexAndFlagsArray = 0;
    RWBuildCLASArgsSB[idx].GeometryIndexAndFlagsIndexBuffer = 0;
    RWBuildCLASArgsSB[idx].OpacityMicromapArray = 0;
    RWBuildCLASArgsSB[idx].OpacityMicromapIndexBuffer = 0;
}