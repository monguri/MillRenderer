#pragma once

#include "ResMesh.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"

class Mesh
{
public:
	Mesh();
	~Mesh();

	bool Init(ID3D12Device* pDevice, const ResMesh& resource);

	void Term();

	void Draw(ID3D12GraphicsCommandList* pCmdList);

	uint32_t GetMaterialId() const;

private:
	VertexBuffer m_VB;
	IndexBuffer m_IB;
	uint32_t m_MaterialId;
	uint32_t m_IndexCount;

	Mesh(const Mesh&) = delete;
	void operator=(const Mesh&) = delete;
};
