#pragma once

#include "d3d12.h"
#include "DirectXMath.h"
#include <string>
#include <vector>

struct MeshVertex
{
public:
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexCoord;
	DirectX::XMFLOAT3 Tangent;

	MeshVertex() = default;

	MeshVertex(
		DirectX::XMFLOAT3 const& Position,
		DirectX::XMFLOAT3 const& Normal,
		DirectX::XMFLOAT2 const& TexCoord,
		DirectX::XMFLOAT3 const& Tangent)
	: Position(Position)
	, Normal(Normal)
	, TexCoord(TexCoord)
	, Tangent(Tangent)
	{}

	static const D3D12_INPUT_LAYOUT_DESC InputLayout;

private:
	static const int InputElementCount = 4;
	static const D3D12_INPUT_ELEMENT_DESC  InputElements[InputElementCount];
};

struct ResMaterial
{
	DirectX::XMFLOAT3 Diffuse;
	DirectX::XMFLOAT3 Specular;
	float Alpha;
	float Shininess;
	std::wstring DiffuseMap;
	std::wstring SpecularMap;
	std::wstring ShininessMap;
	std::wstring NormalMap;
	DirectX::XMFLOAT3 BaseColor;
	std::wstring BaseColorMap;
	std::wstring MetallicRoughnessMap;
};

struct ResMesh
{
	std::vector<MeshVertex> Vertices;
	std::vector<uint32_t> Indices;
	uint32_t MaterialId;
};

//-----------------------------------------------------------------------------
//! @brief      ���b�V�������[�h���܂�.
//!
//! @param[in]      filename        �t�@�C���p�X.
//! @param[out]     meshes          ���b�V���̊i�[��ł�.
//! @param[out]     materials       �}�e���A���̊i�[��ł�.
//! @retval true    ���[�h�ɐ���.
//! @retval false   ���[�h�Ɏ��s.
//-----------------------------------------------------------------------------
bool LoadMesh
(
	const wchar_t* filename,
	std::vector<ResMesh>& meshes,
	std::vector<ResMaterial>& materials
);
