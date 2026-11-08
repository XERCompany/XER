#include "opt_constant_folding.h"

namespace XER::ModelsBuilder::Optimization {

void OptConstantFolding::Apply(GraphEngine::GraphBase& graph) {
  // Collect nodes whose all inputs are produced by Const-type nodes
  std::vector<int64_t> const_foldable;
  for (auto& [id, node] : graph.Nodes()) {
    if (node.op_type == "Constant" || node.op_type == "Const")
      const_foldable.push_back(id);
  }
  // Remove pure constant Identity nodes (no consumers)
  for (int64_t id : const_foldable) {
    auto* n = graph.GetNode(id);
    if (n && n->outputs.empty()) graph.RemoveNode(id);
  }
}

}  // namespace XER::ModelsBuilder::Optimization
