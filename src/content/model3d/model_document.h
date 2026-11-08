#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::Content::Model3D {

// Neutral 3D scene representation — decoupled from Assimp internals so
// renderers (Dawn), exporters and diff tools can work with one format.

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct Vec2 {
	float u = 0.0f;
	float v = 0.0f;
};

struct MeshData {
	std::string name;
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;      // may be empty when absent
	std::vector<Vec2> texcoords;    // may be empty when absent
	std::vector<std::uint32_t> indices;
	std::size_t material_index = 0;
};

struct MaterialData {
	std::string name;
	Vec3 base_color{0.8f, 0.8f, 0.8f};
	float metallic = 0.0f;
	float roughness = 0.5f;
	float opacity = 1.0f;
	bool double_sided = false;
};

// Local transform stored row-major (same layout as aiMatrix4x4).
struct ModelNode {
	std::string name;
	float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	std::vector<std::size_t> mesh_indices;  // indices into ModelScene::meshes
	std::vector<ModelNode> children;
};

struct ModelBounds {
	Vec3 min{0, 0, 0};
	Vec3 max{0, 0, 0};
	bool valid = false;
};

struct ModelScene {
	std::vector<MeshData> meshes;
	std::vector<MaterialData> materials;
	std::vector<ModelNode> roots;
	ModelBounds bounds;              // aggregate, node-local space
	std::string source_path;
	std::string source_format;       // detected extension / importer
	bool loaded = false;
};

class ModelLoader {
public:
	// Loads a model file via Assimp. Post-processing: triangulation,
	// smooth normals, identical-vertex join, UV transform. The node
	// hierarchy and local transforms are preserved (no flattening).
	static bool Load(const std::string& path,
			 ModelScene* scene_out,
			 std::string* error_out = nullptr);

	// Loads from memory; `hint` is an optional format extension
	// ("obj", "fbx", ...) used when detection is ambiguous.
	static bool LoadFromMemory(const void* data,
				   std::size_t size,
				   const char* hint,
				   ModelScene* scene_out,
				   std::string* error_out = nullptr);

	static std::vector<std::string> SupportedImportFormats();
	static std::vector<std::string> SupportedExportFormats();
};

}  // namespace XER::Content::Model3D
