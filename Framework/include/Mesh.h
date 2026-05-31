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
		class DescriptorPool* pPoolGpuVisible,
		const ResMesh& resource
	)
	{
		return Init(pDevice, pCmdList, pPoolGpuVisible, resource, sizeof(CbType));
	}

	void Term();

	void Draw(ID3D12GraphicsCommandList6* pCmdList) const;

	template<typename T>
	T* MapConstantBuffer(uint32_t frameIndex) const
	{
		return m_CB[frameIndex].Map<T>();
	}

	void UnmapConstantBuffer(uint32_t frameIndex) const;

	const DescriptorHandle& GetConstantBufferHandle(uint32_t frameIndex) const;

	uint32_t GetMaterialIdx() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	// MeshletÇÃèÍçáÇÕSBÅAí èÌMeshÇÃèÍçáÇÕVB
	Resource m_VB;
	Resource m_IB;
	Resource m_CB[App::FRAME_COUNT];
	uint32_t m_MaterialIdx;
	size_t m_IndexCount;
	Mobility m_Mobility;
	class DescriptorPool* m_pPoolGpuVisible;
	class DescriptorPool* m_pPoolCpuVisible;

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		const ResMesh& resource,
		size_t cbBufferSize
	);

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;
};
