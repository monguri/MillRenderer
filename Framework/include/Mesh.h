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
		class DescriptorPool* pPoolCpuVisible,
		const ResMesh& resource,
		bool isMeshlet = false
	)
	{
		return Init(pDevice, pCmdList, pPoolGpuVisible, pPoolCpuVisible, resource, sizeof(CbType), isMeshlet);
	}

	void Term();

	void ClearDrawMeshletBBs(ID3D12GraphicsCommandList6* pCmdList) const;
	void DoMeshletCulling(ID3D12GraphicsCommandList6* pCmdList) const;
	void DrawByHWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const;
	void DrawBySWRasterizer(ID3D12GraphicsCommandList6* pCmdList) const;
	//TODO: BoundingSphere関連はすべて現在未使用でデッドコードになっている。World行列に不均一スケールがあると楕円球になり扱いにくいため
	void DrawMeshletBoundingSphere(ID3D12GraphicsCommandList6* pCmdList) const;
	void DrawMeshletAABB(ID3D12GraphicsCommandList6* pCmdList) const;

	template<typename T>
	T* MapConstantBuffer(uint32_t frameIndex) const
	{
		return m_CB[frameIndex].Map<T>();
	}

	void UnmapConstantBuffer(uint32_t frameIndex) const;

	const DescriptorHandle& GetConstantBufferHandle(uint32_t frameIndex) const;
	const DescriptorHandle& GetVertexBufferSBHandle() const;
	const DescriptorHandle& GetIndexBufferSBHandle() const;
	const DescriptorHandle& GetMeshletsSBHandle() const;
	const DescriptorHandle& GetMeshletsVerticesSBHandle() const;
	const DescriptorHandle& GetMeshletsTrianglesBBHandle() const;
	const DescriptorHandle& GetMeshletsBoundingSphereInfosSBHandle() const;
	const DescriptorHandle& GetMeshletsAABBInfosSBHandle() const;
	const DescriptorHandle& GetDrawMeshletIndirectArgBBHandle() const;
	const DescriptorHandle& GetDrawMeshletListBBHandle() const;

	uint32_t GetMaterialId() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	bool m_IsMeshlet = false;
	// Meshletの場合はSB、通常Meshの場合はVB
	Resource m_VB;
	Resource m_IB;
	Resource m_CB[App::FRAME_COUNT];
	Resource m_MeshletsSB;
	Resource m_MeshletsVerticesSB;
	Resource m_MeshletsTrianglesBB;
	Resource m_UnitSphereVB;
	Resource m_UnitSphereIB;
	Resource m_BoundingSphereInfosSB;
	Resource m_UnitCubeVB;
	Resource m_UnitCubeIB;
	Resource m_AABBInfosSB;
	ComPtr<ID3D12CommandSignature> m_pDrawMeshletCmdSig;
	Resource m_DrawMeshletIndirectArgBB;
	Resource m_DrawMeshletListBB;
	uint32_t m_MaterialId;
	size_t m_IndexCount;
	size_t m_SphereIndexCount;
	size_t m_MeshletCount;
	Mobility m_Mobility;
	class DescriptorPool* m_pPoolGpuVisible;
	class DescriptorPool* m_pPoolCpuVisible;

	bool Init
	(
		ID3D12Device* pDevice,
		ID3D12GraphicsCommandList* pCmdList,
		class DescriptorPool* pPoolGpuVisible,
		class DescriptorPool* pPoolCpuVisible,
		const ResMesh& resource,
		size_t cbBufferSize,
		bool isMeshlet
	);

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;

public:
	static const D3D12_INPUT_LAYOUT_DESC PosOnlyInputLayout;

private:
	static const int PosOnlyInputElementCount = 1;
	static const D3D12_INPUT_ELEMENT_DESC PosOnlyInputElements[PosOnlyInputElementCount];

};
