#include "ResMesh.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <codecvt>
#include <cassert>

namespace
{
	std::string ToUTF8(const std::wstring& value)
	{
		int length = WideCharToMultiByte(CP_UTF8, 0U, value.data(), -1, nullptr, 0, nullptr, nullptr);
		char* buffer = new char[length];

		WideCharToMultiByte(CP_UTF8, 0U, value.data(), -1, buffer, length, nullptr, nullptr);

		std::string result(buffer);
		delete[] buffer;
		buffer = nullptr;

		return result;
	}

	std::wstring Convert(const aiString& path)
	{
		wchar_t temp[256] = {};
		size_t size;
		mbstowcs_s(&size, temp, path.C_Str(), 256);
		return std::wstring(temp);
	}

	class MeshLoader
	{
	public:
		MeshLoader();
		~MeshLoader();

		bool Load
		(
			const wchar_t* filename,
			std::vector<ResMesh>& meshes,
			std::vector<ResMaterial>& materials
		);
	
	private:
		void ParseMesh(ResMesh& dstMesh, const aiMesh* pSrcMesh);
		void ParseMaterial(ResMaterial& dstMaterial, const aiMaterial* pSrcMaterial);
	};

	MeshLoader::MeshLoader()
	{
	}

	MeshLoader::~MeshLoader()
	{
	}

	bool MeshLoader::Load
	(
		const wchar_t* filename,
		std::vector<ResMesh>& meshes,
		std::vector<ResMaterial>& materials
	)
	{
		if (filename == nullptr)
		{
			return false;
		}

		const std::string& path = ToUTF8(filename);

		Assimp::Importer importer;
		unsigned int flag = 0;
		flag |= aiProcess_Triangulate;
		flag |= aiProcess_PreTransformVertices;
		flag |= aiProcess_CalcTangentSpace;
		flag |= aiProcess_GenSmoothNormals;
		flag |= aiProcess_GenUVCoords;
		flag |= aiProcess_RemoveRedundantMaterials;
		flag |= aiProcess_OptimizeMeshes;
		flag |= aiProcess_ConvertToLeftHanded;

		const aiScene* pScene = importer.ReadFile(path, flag);
		if (pScene == nullptr)
		{
			return false;
		}

		meshes.clear();
		meshes.resize(pScene->mNumMeshes);

		for (size_t i = 0; i < meshes.size(); i++)
		{
			ParseMesh(meshes[i], pScene->mMeshes[i]);
		}

		materials.clear();
		materials.resize(pScene->mNumMaterials);

		for (size_t i = 0; i < materials.size(); i++)
		{
			ParseMaterial(materials[i], pScene->mMaterials[i]);
		}

		pScene = nullptr;

		return true;
	}

	void MeshLoader::ParseMesh(ResMesh& dstMesh, const aiMesh* pSrcMesh)
	{
		dstMesh.MaterialId = pSrcMesh->mMaterialIndex;

		dstMesh.Vertices.resize(pSrcMesh->mNumVertices);

		aiVector3D zero3D(0.0f, 0.0f, 0.0f);
		for (unsigned int i = 0u; i < pSrcMesh->mNumVertices; i++)
		{
			const aiVector3D* pPosition = &(pSrcMesh->mVertices[i]);
			const aiVector3D* pNormal = &(pSrcMesh->mNormals[i]);
			const aiVector3D* pTexCoord = pSrcMesh->HasTextureCoords(0) ? &(pSrcMesh->mTextureCoords[0][i]) : &zero3D;
			const aiVector3D* pTangent = pSrcMesh->HasTangentsAndBitangents() ? &(pSrcMesh->mTangents[i]) : &zero3D;
			
			dstMesh.Vertices[i] = MeshVertex(
				DirectX::XMFLOAT3(pPosition->x, pPosition->y, pPosition->z),
				DirectX::XMFLOAT3(pNormal->x, pNormal->y, pNormal->z),
				DirectX::XMFLOAT2(pTexCoord->x, pTexCoord->y),
				DirectX::XMFLOAT3(pTangent->x, pTangent->y, pTangent->z)
			);
		}

		dstMesh.Indices.resize(pSrcMesh->mNumFaces * 3);
		for (unsigned int i = 0u; i < pSrcMesh->mNumFaces; i++)
		{
			const aiFace& face = pSrcMesh->mFaces[i];
			assert(face.mNumIndices == 3);

			dstMesh.Indices[i * 3 + 0] = face.mIndices[0];
			dstMesh.Indices[i * 3 + 1] = face.mIndices[1];
			dstMesh.Indices[i * 3 + 2] = face.mIndices[2];
		}
	}

	void MeshLoader::ParseMaterial(ResMaterial& dstMaterial, const aiMaterial* pSrcMaterial)
	{
		{
			aiColor3D color(0.0f, 0.0f, 0.0f);
			if (pSrcMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
			{
				dstMaterial.Diffuse.x = color.r;
				dstMaterial.Diffuse.y = color.g;
				dstMaterial.Diffuse.z = color.b;
			}
			else
			{
				dstMaterial.Diffuse.x = 0.0f;
				dstMaterial.Diffuse.y = 0.0f;
				dstMaterial.Diffuse.z = 0.0f;
			}
		}

		{
			aiColor3D color(0.0f, 0.0f, 0.0f);
			if (pSrcMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
			{
				dstMaterial.Specular.x = color.r;
				dstMaterial.Specular.y = color.g;
				dstMaterial.Specular.z = color.b;
			}
			else
			{
				dstMaterial.Specular.x = 0.0f;
				dstMaterial.Specular.y = 0.0f;
				dstMaterial.Specular.z = 0.0f;
			}
		}

		{
			float shininess = 0.0f;
			if (pSrcMaterial->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
			{
				dstMaterial.Shininess = shininess;
			}
			else
			{
				dstMaterial.Shininess = 0.0f;
			}
		}

		{
			aiString path;

			if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_DIFFUSE(0), path) == AI_SUCCESS)
			{
				dstMaterial.DiffuseMap = Convert(path);
			}
			else
			{
				dstMaterial.DiffuseMap.clear();
			}
		}

		{
			aiString path;

			if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_SPECULAR(0), path) == AI_SUCCESS)
			{
				dstMaterial.SpecularMap = Convert(path);
			}
			else
			{
				dstMaterial.SpecularMap.clear();
			}
		}

		{
			aiString path;

			if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_SHININESS(0), path) == AI_SUCCESS)
			{
				dstMaterial.ShininessMap = Convert(path);
			}
			else
			{
				dstMaterial.ShininessMap.clear();
			}
		}

		{
			aiString path;

			if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_NORMALS(0), path) == AI_SUCCESS)
			{
				dstMaterial.NormalMap = Convert(path);
			}
			else
			{
				if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_HEIGHT(0), path) == AI_SUCCESS)
				{
					dstMaterial.NormalMap = Convert(path);
				}
				else
				{
					dstMaterial.NormalMap.clear();
				}
			}
		}


		{
			aiColor3D color(1.0f, 1.0f, 1.0f);
			if (pSrcMaterial->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
			{
				dstMaterial.BaseColor.x = color.r;
				dstMaterial.BaseColor.y = color.g;
				dstMaterial.BaseColor.z = color.b;
			}
			else
			{
				dstMaterial.BaseColor.x = 1.0f;
				dstMaterial.BaseColor.y = 1.0f;
				dstMaterial.BaseColor.z = 1.0f;
			}
		}

		{
			aiString path;

			if (pSrcMaterial->GetTexture(AI_MATKEY_BASE_COLOR_TEXTURE, &path) == AI_SUCCESS)
			{
				dstMaterial.BaseColorMap = Convert(path);
			}
			else
			{
				dstMaterial.BaseColorMap .clear();
			}
		}

		{
			float metallicFactor = 1.0f;
			if (pSrcMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS)
			{
				dstMaterial.MetallicFactor = metallicFactor;
			}
			else
			{
				dstMaterial.MetallicFactor = 1.0f;
			}
		}

		{
			float roughnessFactor = 1.0f;
			if (pSrcMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS)
			{
				dstMaterial.RoughnessFactor = roughnessFactor;
			}
			else
			{
				dstMaterial.RoughnessFactor = 1.0f;
			}
		}

		{
			aiString path;

			if (pSrcMaterial->GetTexture(AI_MATKEY_METALLIC_TEXTURE, &path) == AI_SUCCESS)
			{
				dstMaterial.MetallicRoughnessMap = Convert(path);
			}
			else
			{
				dstMaterial.MetallicRoughnessMap.clear();
			}
		}

		{
			aiString alphaMode;

			if (pSrcMaterial->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
			{
				std::string opaque("OPAQUE");
				std::string mask("MASK");
				std::string blend("BLEND");
				if (alphaMode == aiString(opaque))
				{
					dstMaterial.AlphaMode = ALPHA_MODE_OPAQUE;
				}
				else if (alphaMode == aiString(mask))
				{
					dstMaterial.AlphaMode = ALPHA_MODE_MASK;
				}
				else if (alphaMode == aiString(blend))
				{
					dstMaterial.AlphaMode = ALPHA_MODE_BLEND;
				}
				else
				{
					dstMaterial.AlphaMode = ALPHA_MODE_OPAQUE;
				}
			}
			else
			{
				dstMaterial.AlphaMode = ALPHA_MODE_OPAQUE;
			}
		}

		{
			float alphaCutoff = 0.0f;
			if (pSrcMaterial->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS)
			{
				dstMaterial.AlphaCutoff = alphaCutoff;
			}
			else
			{
				dstMaterial.AlphaCutoff = 0.0f;
			}
		}

		{
			bool doubleSided = false;
			if (pSrcMaterial->Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS)
			{
				dstMaterial.DoubleSided = doubleSided;
			}
			else
			{
				dstMaterial.DoubleSided = false;
			}
		}
	}
}

const D3D12_INPUT_ELEMENT_DESC MeshVertex::InputElements[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
};

const D3D12_INPUT_LAYOUT_DESC MeshVertex::InputLayout = {
	MeshVertex::InputElements,
	MeshVertex::InputElementCount
};

static_assert(sizeof(MeshVertex) == 44, "Vertex struct/layout mismatch");

bool LoadMesh
(
	const wchar_t* filename,
	std::vector<ResMesh>& meshes,
	std::vector<ResMaterial>& materials
)
{
	MeshLoader loader;
	return loader.Load(filename, meshes, materials);
}
