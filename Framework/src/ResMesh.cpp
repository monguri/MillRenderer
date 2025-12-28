#include "ResMesh.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <metis.h>
#include <codecvt>
#include <cassert>
#include <functional>
#include <set>
#include <map>

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
			bool buildMeshlet,
			bool useMetis,
			std::vector<ResMesh>& meshes,
			std::vector<ResMaterial>& materials
		);
	
	private:
		void ParseMesh(ResMesh& dstMesh, const aiMesh* pSrcMesh);
		void ParseMaterial(ResMaterial& dstMaterial, const aiMaterial* pSrcMaterial);
		void BuildMeshlet(ResMesh& dstMesh, bool useMetis);
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
		bool buildMeshlet,
		bool useMetis,
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
		// TODO:AssimpでglTFファイルから取得したTexCoordのVは通常のVとは上下が逆になっているようなので一旦ここで上下反転させる
		// https://github.com/assimp/assimp/issues/2102
		// https://github.com/assimp/assimp/issues/2849
		// TODO:aiProcess_ConvertToLeftHandedだと法線方向が逆になってライティングがおかしくなった
		flag |= aiProcess_FlipUVs;

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

		if (buildMeshlet)
		{
			for (ResMesh& mesh : meshes)
			{
				BuildMeshlet(mesh, useMetis);
			}
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
				dstMaterial.NormalMap.clear();
			}
		}

		{
			aiString path;

			if (pSrcMaterial->Get(AI_MATKEY_TEXTURE_HEIGHT(0), path) == AI_SUCCESS)
			{
				dstMaterial.HeightMap = Convert(path);
			}
			else
			{
				dstMaterial.HeightMap.clear();
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
			//TODO: gltfのemissiveFactorはこの方法ではとれていない
			aiColor3D emissiveFactor(1, 1, 1);
			if (pSrcMaterial->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveFactor) == AI_SUCCESS)
			{
				dstMaterial.EmissiveFactor.x = emissiveFactor.r;
				dstMaterial.EmissiveFactor.y = emissiveFactor.g;
				dstMaterial.EmissiveFactor.z = emissiveFactor.b;
			}
			else
			{
				dstMaterial.EmissiveFactor.x = 1.0f;
				dstMaterial.EmissiveFactor.y = 1.0f;
				dstMaterial.EmissiveFactor.z = 1.0f;
			}
		}

		{
			aiString path;

			// GLTF以外への対応も考慮してより汎用のキーにしておく
			//if (pSrcMaterial->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &path) == AI_SUCCESS)
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
			aiString path;

			if (pSrcMaterial->GetTexture(aiTextureType_EMISSIVE, 0, &path) == AI_SUCCESS)
			{
				dstMaterial.EmissiveMap = Convert(path);
			}
			else
			{
				dstMaterial.EmissiveMap.clear();
			}
		}

		{
			aiString path;

			// GLTF occlusion texture is this type. It is not aiTextureType_AMBIENT_OCCLUSION.
			if (pSrcMaterial->GetTexture(aiTextureType_LIGHTMAP, 0, &path) == AI_SUCCESS)
			{
				dstMaterial.AmbientOcclusionMap = Convert(path);
			}
			else
			{
				dstMaterial.AmbientOcclusionMap.clear();
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

	void MeshLoader::BuildMeshlet(ResMesh& dstMesh, bool useMetis)
	{
		// NVIDIAの推奨値
		static constexpr uint32_t MAX_VERTS = 64;
		static constexpr uint32_t MAX_TRIS = 126;

		if (useMetis)
		{
			// Metisは分割数を指定して分割する形なので、各分割のVertex数やTriangle数がMAX_VERTSとMAX_TRIS以下になる保証はない
			// よってを上限を小さめにして分割数を大きく計算し余裕を持たせている。
			static constexpr uint32_t MAX_VERTS_FOR_METIS = MAX_VERTS / 2;
			static constexpr uint32_t MAX_TRIS_FOR_METIS = MAX_TRIS / 2;

			if (dstMesh.Vertices.size() <= MAX_VERTS_FOR_METIS || dstMesh.Indices.size() / 3 <= MAX_TRIS_FOR_METIS)
			{
				// Metisは入力されたノード数が分割数以下の場合クラッシュするのでガードする
				// この場合は1つのMeshlet扱いにする
				dstMesh.Meshlets.resize(1);
				dstMesh.MeshletsVertices.resize(dstMesh.Vertices.size());
				dstMesh.MeshletsTriangles.resize(dstMesh.Indices.size());

				meshopt_Meshlet& meshlet = dstMesh.Meshlets[0];
				meshlet.vertex_count = static_cast<unsigned int>(dstMesh.Vertices.size());
				assert(meshlet.vertex_count <= MAX_VERTS);
				meshlet.vertex_offset = 0;
				meshlet.triangle_count = static_cast<unsigned int>(dstMesh.Indices.size() / 3);
				assert(meshlet.triangle_count <= MAX_TRIS);
				meshlet.triangle_offset = 0;

				for (uint32_t i = 0; i < dstMesh.Vertices.size(); i++)
				{
					dstMesh.MeshletsVertices[i] = i;
				}

				// uint8_tに収まるはず
				assert(dstMesh.Vertices.size() < 256);

				for (uint32_t i = 0; i < dstMesh.Indices.size(); i++)
				{
					// uint8_tに収まるはず
					assert(dstMesh.Indices[i] < 256);
					dstMesh.MeshletsTriangles[i] = static_cast<uint8_t>(dstMesh.Indices[i]);
				}
			}
			else
			{
				//
				// METIS_PartGraphKway()用にメッシュのTriangleをノードとし、エッジでの隣接をリンクとして捉えたグラフデータを構築する
				//

				using Edge = std::pair<uint32_t, uint32_t>;
				// 比較ができるように昇順にしておく
				const auto& generateEdge = [](uint32_t idxA, uint32_t idxB)
				{
					return Edge(
						std::min(idxA, idxB),
						std::max(idxA, idxB)
					);
				};

				// Triangleごとのエッジリストと、エッジごとのTriangleリストを構築
				std::map<Edge, std::set<uint32_t>> edgeTrianglesMap;
				uint32_t triangleCount = static_cast<uint32_t>(dstMesh.Indices.size() / 3);

				for (uint32_t triIdx = 0; triIdx < triangleCount; triIdx++)
				{
					uint32_t idx0 = dstMesh.Indices[triIdx * 3 + 0];
					uint32_t idx1 = dstMesh.Indices[triIdx * 3 + 1];
					uint32_t idx2 = dstMesh.Indices[triIdx * 3 + 2];

					const Edge& edge0 = generateEdge(idx0, idx1);
					const Edge& edge1 = generateEdge(idx1, idx2);
					const Edge& edge2 = generateEdge(idx2, idx0);

					edgeTrianglesMap[edge0].insert(triIdx);
					edgeTrianglesMap[edge1].insert(triIdx);
					edgeTrianglesMap[edge2].insert(triIdx);
				}

				// Triangleごとの隣接Triangleリストを構築
				std::vector<std::vector<uint32_t>> triAdjTriList;
				triAdjTriList.resize(triangleCount);
				for (const std::pair<Edge, std::set<uint32_t>>& edgeTrianglesPair : edgeTrianglesMap)
				{
					assert(edgeTrianglesPair.second.size() == 2 || edgeTrianglesPair.second.size() == 1);

					// std::setなのでインデックスでは取り出せずイテレータで取り出すしかない
					for (uint32_t triIdx : edgeTrianglesPair.second)
					{
						for (uint32_t triIdx2 : edgeTrianglesPair.second)
						{
							if (triIdx != triIdx2)
							{
								assert(edgeTrianglesPair.second.size() == 2);
								// 2つのTriangleの双方向で2つ登録される
								triAdjTriList[triIdx].push_back(triIdx2);
							}
						}
					}
				}

				// Metis用のデータに変換
				std::vector<idx_t> adjTriTable;
				std::vector<idx_t> adjTriTableOffsets;
				adjTriTableOffsets.reserve(triangleCount + 1);
				adjTriTableOffsets.emplace_back(0u);

				for (const std::vector<uint32_t>& adjTriList : triAdjTriList)
				{
					assert(adjTriList.size() >= 1 || adjTriList.size() <= 3);

					// append_range()はC++23からなので手動で実装
					for (uint32_t adjTri : adjTriList)
					{
						adjTriTable.emplace_back(static_cast<idx_t>(adjTri));
					}

					adjTriTableOffsets.emplace_back(static_cast<idx_t>(adjTriTableOffsets.back()) + static_cast<idx_t>(adjTriList.size()));
				}

				// Metisでグラフ分割を実行。TriangleをMeshlet分割するのと等価。
				idx_t nCon = 1;
				// MeshOptimiezerと違い、最大頂点数や最大Triangle数を指定できず分割数を指定する方式。
				idx_t nParts = std::max(
					(static_cast<uint32_t>(dstMesh.Vertices.size()) + MAX_VERTS_FOR_METIS - 1) / MAX_VERTS_FOR_METIS,
					(triangleCount + MAX_TRIS_FOR_METIS - 1) / MAX_TRIS_FOR_METIS
				);
				std::vector<idx_t> vwgt(triangleCount, 1);
				std::vector<idx_t> adjwgt(adjTriTableOffsets.back(), 1);
				std::vector<idx_t> options(METIS_NOPTIONS);
				int result = METIS_SetDefaultOptions(options.data());
				assert(result == METIS_OK);
				idx_t edgecut = 0;
				std::vector<idx_t> part(triangleCount);

				result = METIS_PartGraphKway
				(
					reinterpret_cast<idx_t*>(&triangleCount),
					&nCon,
					reinterpret_cast<idx_t*>(adjTriTableOffsets.data()),
					reinterpret_cast<idx_t*>(adjTriTable.data()),
					vwgt.data(),
					nullptr, // vsize
					adjwgt.data(),
					&nParts,
					nullptr, // tpwgts
					nullptr, // ubvec
					options.data(),
					&edgecut,
					part.data()  // part
				);
				assert(result == METIS_OK);

				//
				// partに格納された分割情報を元にMeshOptimizer形式のMeshletデータを構築する
				//

				// 各Meshletの頂点インデックス集合を構築
				// std::setを使っているので自動的に重複は排除され昇順にソートされている
				std::vector<std::set<uint32_t>> meshletVertexSets(nParts);
				for (uint32_t triIdx = 0; triIdx < triangleCount; triIdx++)
				{
					std::set<uint32_t>& vertexSet = meshletVertexSets[part[triIdx]];
					vertexSet.insert(dstMesh.Indices[triIdx * 3 + 0]);
					vertexSet.insert(dstMesh.Indices[triIdx * 3 + 1]);
					vertexSet.insert(dstMesh.Indices[triIdx * 3 + 2]);
				}

				// 各Meshletのローカルインデックスバッファを構築
				std::vector<std::vector<uint8_t>> meshletTriangleLists(nParts);
				for (uint32_t triIdx = 0; triIdx < triangleCount; triIdx++)
				{
					const std::set<uint32_t>& vertexSet = meshletVertexSets[part[triIdx]];

					std::vector<uint8_t>& triangleList = meshletTriangleLists[part[triIdx]];

					// triangleListに入る3頂点のローカルインデックスの挿入順が重要なのでループを分離している
					// 順序が変わるとTriangleの向きが変わってしまう
					uint32_t localIdx0 = 0;
					for (uint32_t vertex : vertexSet)
					{
						if (dstMesh.Indices[triIdx * 3 + 0] == vertex)
						{
							assert(localIdx0 < 256); // uint8_tに収まるはず
							triangleList.emplace_back(localIdx0);
							break;
						}

						localIdx0++;
					}

					uint32_t localIdx1 = 0;
					for (uint32_t vertex : vertexSet)
					{
						if (dstMesh.Indices[triIdx * 3 + 1] == vertex)
						{
							assert(localIdx1 < 256); // uint8_tに収まるはず
							triangleList.emplace_back(localIdx1);
							break;
						}

						localIdx1++;
					}

					uint32_t localIdx2 = 0;
					for (uint32_t vertex : vertexSet)
					{
						if (dstMesh.Indices[triIdx * 3 + 2] == vertex)
						{
							assert(localIdx2 < 256); // uint8_tに収まるはず
							triangleList.emplace_back(localIdx2);
							break;
						}

						localIdx2++;
					}
				}

				dstMesh.Meshlets.resize(nParts);

				unsigned int vertexOffset = 0;
				unsigned int triangleOffset = 0;
				for (uint32_t meshletIdx = 0; meshletIdx < static_cast<uint32_t>(nParts); meshletIdx++)
				{
					meshopt_Meshlet& meshlet = dstMesh.Meshlets[meshletIdx];

					const std::set<uint32_t>& vertexSet = meshletVertexSets[meshletIdx];
					meshlet.vertex_count = static_cast<unsigned int>(vertexSet.size());
					assert(meshlet.vertex_count <= MAX_VERTS);
					meshlet.vertex_offset = vertexOffset;
					vertexOffset += meshlet.vertex_count;

					dstMesh.MeshletsVertices.reserve(dstMesh.MeshletsVertices.size() + vertexSet.size());
					for (uint32_t vertex : vertexSet)
					{
						dstMesh.MeshletsVertices.emplace_back(vertex);
					}

					const std::vector<uint8_t>& triangleList = meshletTriangleLists[meshletIdx];
					assert(triangleList.size() % 3 == 0);
					meshlet.triangle_count = static_cast<unsigned int>(triangleList.size() / 3);
					assert(meshlet.triangle_count <= MAX_TRIS);
					meshlet.triangle_offset = triangleOffset;
					triangleOffset += meshlet.triangle_count * 3;

					dstMesh.MeshletsTriangles.reserve(dstMesh.MeshletsTriangles.size() + triangleList.size());
					for (uint8_t localIdx : triangleList)
					{
						dstMesh.MeshletsTriangles.emplace_back(localIdx);
					}
				}
			}
		}
		else
		{
			size_t indexCount = dstMesh.Indices.size();
			size_t vertexCount = dstMesh.Vertices.size();

			size_t maxMeshletCount = meshopt_buildMeshletsBound(indexCount, MAX_VERTS, MAX_TRIS);

			dstMesh.Meshlets.resize(maxMeshletCount);
			dstMesh.MeshletsVertices.resize(indexCount);
			dstMesh.MeshletsTriangles.resize(indexCount);

			static_assert(sizeof(float) * 3 == sizeof(DirectX::XMFLOAT3));
			std::vector<float> vertexPositions(vertexCount * 3);
			for (size_t i = 0; i < vertexCount; i++)
			{
				const MeshVertex& vert = dstMesh.Vertices[i];
				memcpy(&vertexPositions[3 * i], &vert.Position, sizeof(DirectX::XMFLOAT3));
			}

			// MeshletsVerticesとMeshletsTrianglesの型がuint32_tとuint8_tなのでstatic_assertで確認しておく
			static_assert(sizeof(unsigned int) == sizeof(uint32_t));
			static_assert(sizeof(unsigned char) == sizeof(uint8_t));

			size_t meshletCount = meshopt_buildMeshlets(
				dstMesh.Meshlets.data(),
				dstMesh.MeshletsVertices.data(),
				dstMesh.MeshletsTriangles.data(),
				dstMesh.Indices.data(),
				indexCount,
				vertexPositions.data(),
				vertexCount,
				sizeof(float) * 3,
				MAX_VERTS,
				MAX_TRIS,
				0.0f // cone cullingを使わない
			);

			// shrink to fit
			const meshopt_Meshlet& lastMeshlet = dstMesh.Meshlets[meshletCount - 1];
			dstMesh.MeshletsVertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
			dstMesh.MeshletsTriangles.resize(lastMeshlet.triangle_offset + lastMeshlet.triangle_count * 3);

			dstMesh.Meshlets.resize(meshletCount);
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
	bool buildMeshlet,
	bool useMetis,
	std::vector<ResMesh>& meshes,
	std::vector<ResMaterial>& materials
)
{
	MeshLoader loader;
	return loader.Load(filename, buildMeshlet, useMetis, meshes, materials);
}
