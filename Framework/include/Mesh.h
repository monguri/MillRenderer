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
	uint32_t m_MaterialId;
	size_t m_IndexCount;
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

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;
};
