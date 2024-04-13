#pragma once

#include "App.h"
#include "ResMesh.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ConstantBuffer.h"

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

	bool Init
	(
		ID3D12Device* pDevice,
		class DescriptorPool* pPool,
		const ResMesh& resource,
		size_t cbBufferSize
	);

	void Term();

	void Draw(ID3D12GraphicsCommandList* pCmdList);

	void* GetBufferPtr(uint32_t frameIndex) const;

	template<typename T>
	T* GetBufferPtr(uint32_t frameIndex) const
	{
		return reinterpret_cast<T*>(GetBufferPtr(frameIndex));
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetConstantBufferHandle(uint32_t frameIndex) const;

	uint32_t GetMaterialId() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	VertexBuffer m_VB;
	IndexBuffer m_IB;
	ConstantBuffer m_CB[App::FRAME_COUNT];
	uint32_t m_MaterialId;
	uint32_t m_IndexCount;
	Mobility m_Mobility;
	class DescriptorPool* m_pPool;

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;
};
