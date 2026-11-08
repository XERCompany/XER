#include "dependency_scanner.h"

#include <unordered_set>
#include <string>

namespace XER::ModelsBuilder::Reader::Schema {

DependencyScanner::ScanResult DependencyScanner::Scan(
    const Core::Model& model) const {
  ScanResult result;
  const auto& layers = model.GetLayers();

  // Track layer names for orphan/missing detection.
  std::unordered_set<std::string> layer_names;
  for (const auto& layer : layers) {
    const std::string& name = layer->GetLayerName();
    if (!name.empty()) {
      layer_names.insert(name);
    }
  }

  // In Sequential models each layer is implicitly connected to the next;
  // every named layer is both "produced" and "consumed" — no orphans or
  // missing tensors unless a layer has no name at all.
  for (const auto& layer : layers) {
    const std::string& name = layer->GetLayerName();
    if (name.empty()) {
      // Unnamed layer cannot be referenced — treat as orphaned.
      result.orphaned.push_back("<unnamed>");
      result.ok = false;
    }
  }

  return result;
}

}  // namespace XER::ModelsBuilder::Reader::Schema
