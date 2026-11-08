#include "model_diff.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include <re2/re2.h>

namespace XER::Content::ModelDiff {
namespace {

using Model3D::ModelNode;
using Model3D::ModelScene;

// Mesh identity: prefer the name; unnamed meshes fall back to their
// index so two anonymous meshes don't collapse into one key.
std::string MeshKey(const Model3D::MeshData& m, std::size_t index) {
	return m.name.empty() ? "#" + std::to_string(index) : m.name;
}

std::unordered_map<std::string, MeshSignature> MeshSignatures(
    const ModelScene& scene) {
	std::unordered_map<std::string, MeshSignature> out;
	out.reserve(scene.meshes.size());
	for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
		const auto& m = scene.meshes[i];
		MeshSignature sig;
		sig.name = MeshKey(m, i);
		sig.vertices = m.positions.size();
		sig.indices = m.indices.size();
		sig.material_index = m.material_index;
		out[sig.name] = sig;
	}
	return out;
}

void CollectNames(const ModelScene& scene, std::vector<std::string>* out) {
	for (const auto& m : scene.meshes) out->push_back(m.name);
	for (const auto& mt : scene.materials) out->push_back(mt.name);
	std::function<void(const ModelNode&)> walk =
	    [&](const ModelNode& n) {
		    out->push_back(n.name);
		    for (const auto& c : n.children) walk(c);
	    };
	for (const auto& r : scene.roots) walk(r);
}

void WalkNodes(const ModelNode& node, const std::string& prefix,
               std::vector<std::string>* out) {
	const std::string path =
	    prefix.empty() ? node.name : prefix + "/" + node.name;
	out->push_back(path);
	for (const auto& c : node.children) WalkNodes(c, path, out);
}

std::size_t CountNodes(const std::vector<ModelNode>& nodes) {
	std::size_t n = 0;
	for (const auto& node : nodes) {
		n += 1 + CountNodes(node.children);
	}
	return n;
}

float BoundsDelta(const Model3D::ModelBounds& a,
                  const Model3D::ModelBounds& b) {
	if (a.valid != b.valid) return 1.0f;
	if (!a.valid) return 0.0f;
	const auto d = [](float x, float y) {
		return std::fabs(x - y);
	};
	return std::max({d(a.min.x, b.min.x), d(a.min.y, b.min.y),
	                 d(a.min.z, b.min.z), d(a.max.x, b.max.x),
	                 d(a.max.y, b.max.y), d(a.max.z, b.max.z)});
}

}  // namespace

void ModelDiff::Compare(const ModelScene& a, const ModelScene& b,
                        DiffStats* out) {
	if (!out) return;
	*out = DiffStats{};

	out->meshes_a = a.meshes.size();
	out->meshes_b = b.meshes.size();
	out->materials_a = a.materials.size();
	out->materials_b = b.materials.size();
	out->nodes_a = CountNodes(a.roots);
	out->nodes_b = CountNodes(b.roots);

	const auto sa = MeshSignatures(a);
	const auto sb = MeshSignatures(b);

	for (const auto& [key, sig_b] : sb) {
		const auto it = sa.find(key);
		if (it == sa.end()) {
			out->added_meshes.push_back(key);
		} else if (it->second.vertices != sig_b.vertices ||
		           it->second.indices != sig_b.indices ||
		           it->second.material_index != sig_b.material_index) {
			out->changed_meshes.push_back(key);
		}
	}
	for (const auto& [key, sig_a] : sa) {
		if (sb.find(key) == sb.end()) out->removed_meshes.push_back(key);
	}

	std::sort(out->added_meshes.begin(), out->added_meshes.end());
	std::sort(out->removed_meshes.begin(), out->removed_meshes.end());
	std::sort(out->changed_meshes.begin(), out->changed_meshes.end());

	out->bounds_delta = BoundsDelta(a.bounds, b.bounds);
	out->bounds_changed = out->bounds_delta > 1e-6f;
}

std::vector<std::string> ModelDiff::SearchNames(
    const ModelScene& scene, const std::string& pattern) {
	std::vector<std::string> names;
	CollectNames(scene, &names);

	RE2 re(pattern, RE2::Quiet);
	if (!re.ok()) return {};

	std::vector<std::string> matches;
	for (const auto& n : names) {
		if (n.empty()) continue;
		if (RE2::PartialMatch(n, re)) matches.push_back(n);
	}
	std::sort(matches.begin(), matches.end());
	matches.erase(std::unique(matches.begin(), matches.end()),
	              matches.end());
	return matches;
}

std::vector<std::string> ModelDiff::NodePaths(const ModelScene& scene) {
	std::vector<std::string> out;
	for (const auto& r : scene.roots) WalkNodes(r, "", &out);
	return out;
}

}  // namespace XER::Content::ModelDiff
