#pragma once

#include "App.h"
#include "ResMesh.h"
#include "Resource.h"

enum Mobility
{
	Static = 0,
	Movable,
};

class Mesh
{
public:
	Mesh();
	~Mesh();

	template<typename CbType>
	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPool,
		const ResMesh& resource,
		bool isMeshlet = false
	)
	{
		return Init(pDevice, pCmdList, pPool, resource, sizeof(CbType), isMeshlet);
	}

	void Term();

	void DrawByHWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const;
	void DrawBySWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const;
	void DrawMeshletBoundingSphere(ID3D12GraphicsCommandList6* pCmdList) const;

	template<typename T>
	T* MapConstantBuffer(uint32_t frameIndex) const
	{
		return m_CB[frameIndex].Map<T>();
	}

	void UnmapConstantBuffer(uint32_t frameIndex) const;

	const DescriptorHandle& GetConstantBufferHandle(uint32_t frameIndex) const;
	const DescriptorHandle& GetVertexBufferSBHandle() const;
	const DescriptorHandle& GetIndexBufferSBHandle() const;
	const DescriptorHandle& GetMesletsSBHandle() const;
	const DescriptorHandle& GetMesletsVerticesSBHandle() const;
	const DescriptorHandle& GetMesletsTrianglesBBHandle() const;
	const DescriptorHandle& GetMeshletsBoundingSphereInfosSBHandle() const;

	uint32_t GetMaterialId() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	bool m_IsMeshlet = false;
	// MeshletÇÃèÍçáÇÕSBÅAí èÌMeshÇÃèÍçáÇÕVB
	Resource m_VB;
	Resource m_IB;
	Resource m_CB[App::FRAME_COUNT];
	Resource m_MeshletsSB;
	Resource m_MeshletsVerticesSB;
	Resource m_MeshletsTrianglesBB;
	std::vector<Resource> m_BoundingSphereVBs;
	std::vector<Resource> m_BoundingSphereIBs;
	Resource m_SphereVB;
	Resource m_SphereIB;
	Resource m_BoundingSphereInfosSB;
	uint32_t m_MaterialId;
	size_t m_IndexCount;
	size_t m_SphereIndexCount;
	size_t m_MeshletCount;
	Mobility m_Mobility;
	class DescriptorPool* m_pPool;

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPool,
		const ResMesh& resource,
		size_t cbBufferSize,
		bool isMeshlet
	);

	static void CreateSphere(uint32_t segmentCount, std::vector<struct DirectX::XMFLOAT3>& outVertices, std::vector<uint32_t>& outIndices);
	static void CreateBoundingSphere(const meshopt_Bounds& meshletBounds, std::vector<struct DirectX::XMFLOAT3>& outVertices, std::vector<uint32_t>& outIndices);

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;

public:
	static const D3D12_INPUT_LAYOUT_DESC WireframeInputLayout;

private:
	static const int WireframeInputElementCount = 1;
	static const D3D12_INPUT_ELEMENT_DESC WireframeInputElements[WireframeInputElementCount];

};
