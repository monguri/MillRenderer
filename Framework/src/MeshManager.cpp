#include "MeshManager.h"
#include "DescriptorPool.h"
#include "ResMesh.h"
#include "Material.h"

#include <DirectXHelpers.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

namespace
{
	// DirectXTK12ÅAGeometry.cpp/hÇÃDirectX::ComputeBoxÇéQçlÇ…ÇµÇƒÇ¢ÇÈ
	void CreateUnitCubeMesh(std::vector<struct Vector3>& outVertices, std::vector<uint32_t>& outIndices)
	{
		using namespace DirectX;

		outVertices.clear();
		outIndices.clear();

		// A box has six faces, each one pointing in a different direction.
		constexpr uint32_t FACE_COUNT = 6;

		static const XMVECTORF32 FACE_NORMALS[FACE_COUNT] =
		{
			{ { {  0,  0,  1, 0 } } },
			{ { {  0,  0, -1, 0 } } },
			{ { {  1,  0,  0, 0 } } },
			{ { { -1,  0,  0, 0 } } },
			{ { {  0,  1,  0, 0 } } },
			{ { {  0, -1,  0, 0 } } },
		};

		outVertices.reserve(FACE_COUNT * 4);
		outIndices.reserve(FACE_COUNT * 6);

		// Create each face in turn.
		for (uint32_t i = 0; i < FACE_COUNT; i++)
		{
			const XMVECTOR normal = FACE_NORMALS[i];

			// Get two vectors perpendicular both to the face normal and to each other.
			const XMVECTORF32 basis = (i >= 4) ? XMVECTORF32{{{0.0f, 0.0f, 1.0f, 0.0f}}} : XMVECTORF32{{{0.0f, 1.0f, 0.0f, 0.0f}}};

			const XMVECTOR side1 = XMVector3Cross(normal, basis);
			const XMVECTOR side2 = XMVector3Cross(normal, side1);

			// Six indices (two triangles) per face.
			const uint32_t vbase = static_cast<uint32_t>(outVertices.size());
			outIndices.emplace_back(vbase + 0);
			outIndices.emplace_back(vbase + 1);
			outIndices.emplace_back(vbase + 2);

			outIndices.emplace_back(vbase + 0);
			outIndices.emplace_back(vbase + 2);
			outIndices.emplace_back(vbase + 3);

			// Four vertices per face.
			// (normal - side1 - side2)
			const XMVECTOR& v0 = XMVectorSubtract(XMVectorSubtract(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v0), XMVectorGetY(v0), XMVectorGetZ(v0));

			// (normal - side1 + side2)
			const XMVECTOR& v1 = XMVectorAdd(XMVectorSubtract(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v1), XMVectorGetY(v1), XMVectorGetZ(v1));

			// (normal + side1 + side2)
			const XMVECTOR& v2 = XMVectorAdd(XMVectorAdd(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v2), XMVectorGetY(v2), XMVectorGetZ(v2));

			// (normal + side1 - side2)
			const XMVECTOR& v3 = XMVectorSubtract(XMVectorAdd(normal, side1), side2);
			outVertices.emplace_back(XMVectorGetX(v3), XMVectorGetY(v3), XMVectorGetZ(v3));
		}
	}
}

MeshManager::~MeshManager()
{
	Term();
}

bool MeshManager::Init
(
	ID3D12Device* pDevice,
	ID3D12GraphicsCommandList* pCmdList,
	class DescriptorPool* pPoolGpuVisible,
	class DescriptorPool* pPoolCpuVisible,
	const std::vector<struct ResMesh>& resMeshes,
	const std::vector<struct ResMaterial>& resMaterials,
	size_t cbBufferSize
)
{
	assert(pDevice != nullptr);
	assert(pPoolGpuVisible != nullptr);
	assert(pPoolCpuVisible != nullptr);
	assert(cbBufferSize > 0);

	m_pPoolGpuVisible = pPoolGpuVisible;
	m_pPoolGpuVisible->AddRef();

	m_pPoolCpuVisible = pPoolCpuVisible;
	m_pPoolCpuVisible->AddRef();

	return true;
}

void MeshManager::Term()
{
	for (Material* pMat : m_pMaterials)
	{
		if (pMat != nullptr)
		{
			pMat->Term();
		}
	}

	if (m_pPoolGpuVisible != nullptr)
	{
		m_pPoolGpuVisible->Release();
		m_pPoolGpuVisible = nullptr;
	}

	if (m_pPoolCpuVisible != nullptr)
	{
		m_pPoolCpuVisible->Release();
		m_pPoolCpuVisible = nullptr;
	}

	for (Resource& VB : m_VBs)
	{
		VB.Term();
	}

	for (Resource& IB : m_IBs)
	{
		IB.Term();
	}

	for (Resource& CB : m_CBs)
	{
		CB.Term();
	}

	for (Resource& SB : m_MeshletsSBs)
	{
		SB.Term();
	}

	for (Resource& SB : m_MeshletsSBs)
	{
		SB.Term();
	}

	for (Resource& SB : m_MeshletsVerticesSBs)
	{
		SB.Term();
	}

	for (Resource& SB : m_MeshletsTrianglesSBs)
	{
		SB.Term();
	}

	for (Resource& SB : m_MeshletsAABBInfosSBs)
	{
		SB.Term();
	}

	m_UnitCubeVB.Term();
	m_UnitCubeIB.Term();

	m_DrawMeshletIndirectArgBB.Term();
	m_DrawMeshletSB.Term();
}
