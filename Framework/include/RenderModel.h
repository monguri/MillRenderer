#pragma once
// DirectXTKにModel.hがあり、名前が重複するとエラーになるのでしょうがなくRenderModel.h/cppにしている

#include <d3d12.h>
#include <vector>

class Model
{
public:
	Model();
	~Model();

	// TODO:Init()でResMeshやResMaterialの配列を受け取って中でMeshやMaterial配列を作ってもいいがまだやってない
	void Term();

	size_t GetMeshCount() const;

	class Mesh* GetMesh(size_t meshIdx) const;
	class Material* GetMaterial(size_t matIdx) const;

	void SetMeshes(const std::vector<class Mesh*>& meshes);
	void SetMaterials(const std::vector<class Material*>& materials);

private:
	std::vector<class Mesh*> m_pMeshes;
	std::vector<class Material*> m_pMaterials;

	Model(const Model&) = delete;
	void operator=(const Model&) = delete;
};
