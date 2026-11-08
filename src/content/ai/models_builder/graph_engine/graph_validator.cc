#include "graph_validator.h"

namespace XER::ModelsBuilder::GraphEngine {

std::vector<ValidationError> GraphValidator::Validate(const GraphBase& graph) {
  std::vector<ValidationError> errors;

  if (graph.NodeCount() == 0) {
    errors.push_back({"Graph is empty"});
    return errors;
  }

  if (HasCycle(graph))
    errors.push_back({"Graph contains a cycle"});

  // Check for duplicate node names
  std::unordered_map<std::string, int> name_count;
  for (auto& [id, node] : graph.Nodes()) name_count[node.name]++;
  for (auto& [name, cnt] : name_count)
    if (cnt > 1 && !name.empty())
      errors.push_back({"Duplicate node name: " + name});

  return errors;
}

bool GraphValidator::HasCycle(const GraphBase& graph) {
  try { graph.TopologicalSort(); return false; }
  catch (...) { return true; }
}

}  // namespace XER::ModelsBuilder::GraphEngine
