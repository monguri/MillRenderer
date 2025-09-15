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

	void Draw(ID3D12GraphicsCommandList* pCmdList) const;

	template<typename T>
	T* MapConstantBuffer(uint32_t frameIndex) const
	{
		return m_CB[frameIndex].Map<T>();
	}

	void UnmapConstantBuffer(uint32_t frameIndex) const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetConstantBufferHandle(uint32_t frameIndex) const;

	uint32_t GetMaterialId() const;
	size_t GetVertexCount() const;
	size_t GetIndexCount() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	bool m_IsMeshlet = false;
	Resource m_VB;
	Resource m_IB;
	Resource m_CB[App::FRAME_COUNT];
	uint32_t m_MaterialId;
	size_t m_VertexCount;
	size_t m_IndexCount;
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
