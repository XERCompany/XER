#include "model_document.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace XER::Content::Model3D {
namespace {

constexpr unsigned int kLoadFlags =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals | aiProcess_SortByPType |
    aiProcess_TransformUVCoords | aiProcess_ValidateDataStructure;

Vec3 ToVec3(const aiVector3D& v) {
	return {v.x, v.y, v.z};
}

Vec2 ToVec2(const aiVector3D& v) {
	return {v.x, v.y};
}

void ConvertNode(const aiNode* node, ModelNode* out) {
	if (!node || !out) return;
	out->name = node->mName.C_Str();
	std::memcpy(out->transform, &node->mTransformation, sizeof(aiMatrix4x4));
	out->mesh_indices.reserve(node->mNumMeshes);
	for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
		out->mesh_indices.push_back(node->mMeshes[i]);
	}
	out->children.resize(node->mNumChildren);
	for (unsigned int i = 0; i < node->mNumChildren; ++i) {
		ConvertNode(node->mChildren[i], &out->children[i]);
	}
}

MaterialData ConvertMaterial(const aiMaterial* mat, std::size_t index) {
	MaterialData result;
	aiString name;
	if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
		result.name = name.C_Str();
	}
	if (result.name.empty()) {
		result.name = "material_" + std::to_string(index);
	}

	aiColor4D diffuse(0.8f, 0.8f, 0.8f, 1.0f);
	if (mat->Get(AI_MATKEY_BASE_COLOR, diffuse) != AI_SUCCESS) {
		mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
	}
	result.base_color = {diffuse.r, diffuse.g, diffuse.b};
	result.opacity = diffuse.a;

	float metallic = 0.0f;
	if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) != AI_SUCCESS) {
		metallic = 0.0f;
	}
	result.metallic = metallic;

	float roughness = 0.5f;
	if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) != AI_SUCCESS) {
		// Fall back to the classic shininess heuristic.
		float shininess = 0.0f;
		if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f) {
			result.roughness = 1.0f - std::min(1.0f, shininess / 128.0f);
		}
	}
	result.roughness = std::clamp(roughness, 0.0f, 1.0f);
	result.metallic = std::clamp(result.metallic, 0.0f, 1.0f);

	int two_sided = 0;
	if (mat->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
		result.double_sided = two_sided != 0;
	}
	return result;
}

void ComputeBounds(const aiScene* scene, ModelBounds* bounds) {
	bounds->valid = false;
	const float inf = std::numeric_limits<float>::infinity();
	Vec3 lo{inf, inf, inf};
	Vec3 hi{-inf, -inf, -inf};
	bool any = false;
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
		const aiMesh* mesh = scene->mMeshes[m];
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
			const aiVector3D& p = mesh->mVertices[v];
			lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
			hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
			any = true;
		}
	}
	if (any) {
		bounds->min = lo;
		bounds->max = hi;
		bounds->valid = true;
	}
}

bool ConvertScene(const aiScene* scene, const std::string& path,
		  const std::string& format_hint, ModelScene* out,
		  std::string* error) {
	if (!scene || !out) {
		if (error) *error = "null scene or output";
		return false;
	}
	*out = ModelScene{};
	out->source_path = path;
	out->source_format = format_hint;

	out->materials.reserve(scene->mNumMaterials);
	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		out->materials.push_back(ConvertMaterial(scene->mMaterials[i], i));
	}

	out->meshes.reserve(scene->mNumMeshes);
	for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
		const aiMesh* mesh = scene->mMeshes[m];
		MeshData md;
		md.name = mesh->mName.C_Str();
		if (md.name.empty()) {
			md.name = "mesh_" + std::to_string(m);
		}
		md.material_index = mesh->mMaterialIndex < out->materials.size()
					    ? mesh->mMaterialIndex
					    : 0;
		md.positions.reserve(mesh->mNumVertices);
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
			md.positions.push_back(ToVec3(mesh->mVertices[v]));
		}
		if (mesh->HasNormals()) {
			md.normals.reserve(mesh->mNumVertices);
			for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
				md.normals.push_back(ToVec3(mesh->mNormals[v]));
			}
		}
		if (mesh->HasTextureCoords(0)) {
			md.texcoords.reserve(mesh->mNumVertices);
			for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
				md.texcoords.push_back(ToVec2(mesh->mTextureCoords[0][v]));
			}
		}
		md.indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			const aiFace& face = mesh->mFaces[f];
			for (unsigned int k = 0; k < face.mNumIndices; ++k) {
				md.indices.push_back(face.mIndices[k]);
			}
		}
		out->meshes.push_back(std::move(md));
	}

	if (scene->mRootNode) {
		out->roots.emplace_back();
		ConvertNode(scene->mRootNode, &out->roots.back());
	}
	ComputeBounds(scene, &out->bounds);
	out->loaded = true;
	return true;
}

std::vector<std::string> SplitExtensions(const char* list) {
	std::vector<std::string> result;
	if (!list) return result;
	std::string s(list);
	size_t pos = 0;
	while (pos < s.size()) {
		size_t semi = s.find(';', pos);
		std::string ext = s.substr(pos, semi == std::string::npos ? semi : semi - pos);
		// Entries look like "*.3d" / "3d" — strip wildcard and dot.
		if (!ext.empty() && ext.front() == '*') ext.erase(ext.begin());
		if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
		if (!ext.empty()) result.push_back(ext);
		if (semi == std::string::npos) break;
		pos = semi + 1;
	}
	return result;
}

}  // namespace

bool ModelLoader::Load(const std::string& path, ModelScene* scene_out,
		       std::string* error_out) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, kLoadFlags);
	if (!scene) {
		if (error_out) *error_out = importer.GetErrorString();
		return false;
	}
	const size_t dot = path.find_last_of('.');
	const std::string format =
	    dot == std::string::npos ? std::string{} : path.substr(dot + 1);
	return ConvertScene(scene, path, format, scene_out, error_out);
}

bool ModelLoader::LoadFromMemory(const void* data, std::size_t size,
				 const char* hint, ModelScene* scene_out,
				 std::string* error_out) {
	if (!data || size == 0) {
		if (error_out) *error_out = "empty input";
		return false;
	}
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFileFromMemory(
	    data, size, kLoadFlags, hint ? hint : "");
	if (!scene) {
		if (error_out) *error_out = importer.GetErrorString();
		return false;
	}
	return ConvertScene(scene, "<memory>", hint ? hint : "", scene_out, error_out);
}

std::vector<std::string> ModelLoader::SupportedImportFormats() {
	Assimp::Importer importer;
	std::string list;
	importer.GetExtensionList(list);
	return SplitExtensions(list.c_str());
}

std::vector<std::string> ModelLoader::SupportedExportFormats() {
	Assimp::Exporter exporter;
	std::vector<std::string> result;
	const std::size_t count = exporter.GetExportFormatCount();
	for (std::size_t i = 0; i < count; ++i) {
		if (const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i)) {
			if (desc->id && desc->id[0] != '\0') result.emplace_back(desc->id);
		}
	}
	return result;
}

}  // namespace XER::Content::Model3D
