#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "content/model3d/model_document.h"

namespace XER::Content::ModelDiff {

// Structural diff between two neutral ModelScenes (model3d). Compares
// mesh/material/node inventories by name, vertex/index counts, and the
// aggregate bounds — enough to answer "what changed between model v1 and
// v2" without touching Assimp internals. RE2 provides the name search.

struct MeshSignature {
	std::string name;
	std::size_t vertices = 0;
	std::size_t indices = 0;
	std::size_t material_index = 0;
};

struct DiffStats {
	// Inventory sizes.
	std::size_t meshes_a = 0;
	std::size_t meshes_b = 0;
	std::size_t materials_a = 0;
	std::size_t materials_b = 0;
	std::size_t nodes_a = 0;
	std::size_t nodes_b = 0;

	// Names present only in B / only in A.
	std::vector<std::string> added_meshes;
	std::vector<std::string> removed_meshes;

	// Names present in both but with different vertex/index/material
	// signature.
	std::vector<std::string> changed_meshes;

	// Aggregate bounds delta (max |min|/|max| component difference).
	bool bounds_changed = false;
	float bounds_delta = 0.0f;

	bool empty() const {
		return added_meshes.empty() && removed_meshes.empty() &&
		       changed_meshes.empty() && !bounds_changed &&
		       meshes_a == meshes_b && materials_a == materials_b &&
		       nodes_a == nodes_b;
	}
};

class ModelDiff {
public:
	// Fills `out` with the structural difference between scenes a and b.
	// Mesh matching is by name; unnamed meshes are matched by position.
	static void Compare(const Model3D::ModelScene& a,
			    const Model3D::ModelScene& b, DiffStats* out);

	// Returns names of meshes, materials and nodes matching the RE2
	// pattern (partial match). Invalid patterns yield an empty result.
	static std::vector<std::string> SearchNames(
	    const Model3D::ModelScene& scene, const std::string& pattern);

	// Flattens the node forest into "parent/child"-style paths
	// ("root", "root/child", ...). Useful for reports and UI trees.
	static std::vector<std::string> NodePaths(const Model3D::ModelScene& scene);
};

}  // namespace XER::Content::ModelDiff
