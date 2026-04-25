#pragma once

#include "d3d12.h"
#include <SimpleMath.h>
#include <meshoptimizer.h>
#include <string>
#include <vector>

struct MeshVertex
{
public:
	DirectX::SimpleMath::Vector3 Position;
	DirectX::SimpleMath::Vector3 Normal;
	DirectX::SimpleMath::Vector2 TexCoord;
	DirectX::SimpleMath::Vector3 Tangent;

	MeshVertex() = default;

	MeshVertex(
		DirectX::SimpleMath::Vector3 const& Position,
		DirectX::SimpleMath::Vector3 const& Normal,
		DirectX::SimpleMath::Vector2 const& TexCoord,
		DirectX::SimpleMath::Vector3 const& Tangent)
	: Position(Position)
	, Normal(Normal)
	, TexCoord(TexCoord)
	, Tangent(Tangent)
	{}

	static const D3D12_INPUT_LAYOUT_DESC InputLayout;

private:
	static const int InputElementCount = 4;
	static const D3D12_INPUT_ELEMENT_DESC InputElements[InputElementCount];
};

enum ALPHA_MODE
{
	ALPHA_MODE_OPAQUE = 0,
	ALPHA_MODE_MASK,
	ALPHA_MODE_BLEND,

	ALPHA_MODE_COUNT
};

struct ResMaterial
{
	DirectX::SimpleMath::Vector3 Diffuse;
	DirectX::SimpleMath::Vector3 Specular;
	float Alpha;
	float Shininess;
	std::wstring DiffuseMap;
	std::wstring SpecularMap;
	std::wstring ShininessMap;
	std::wstring NormalMap;
	std::wstring HeightMap;
	DirectX::SimpleMath::Vector3 BaseColor;
	std::wstring BaseColorMap;
	float MetallicFactor;
	float RoughnessFactor;
	DirectX::SimpleMath::Vector3 EmissiveFactor;
	std::wstring MetallicRoughnessMap;
	std::wstring EmissiveMap;
	std::wstring AmbientOcclusionMap;
	ALPHA_MODE AlphaMode;
	float AlphaCutoff;
	bool DoubleSided;
};

struct AABB
{
	DirectX::SimpleMath::Vector3 Center;
	DirectX::SimpleMath::Vector3 HalfExtent;
};

struct ResMesh
{
	std::vector<MeshVertex> Vertices;
	std::vector<uint32_t> Indices;

	// データにはmeshletoptimezerのものをそのまま使う
	std::vector<meshopt_Meshlet> Meshlets;
	std::vector<uint32_t> MeshletsVertices;
	std::vector<uint8_t> MeshletsTriangles;
	std::vector<meshopt_Bounds> Bounds;
	std::vector<AABB> AABBs;

	uint32_t MaterialId;
};

bool LoadMesh
(
	const wchar_t* filename,
	bool buildMeshlet,
	bool useMetis,
	std::vector<ResMesh>& meshes,
	std::vector<ResMaterial>& materials
);
