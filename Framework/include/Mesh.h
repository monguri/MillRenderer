#pragma once

#include "ResMesh.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"

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

	bool Init(ID3D12Device* pDevice, const ResMesh& resource);

	void Term();

	void Draw(ID3D12GraphicsCommandList* pCmdList);

	uint32_t GetMaterialId() const;
	Mobility GetMobility() const;
	void SetMobility(Mobility mobility);

private:
	VertexBuffer m_VB;
	IndexBuffer m_IB;
	uint32_t m_MaterialId;
	uint32_t m_IndexCount;
	Mobility m_Mobility;

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;
};
