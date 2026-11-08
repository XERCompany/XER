#include "graph_optimizer.h"

#include <unordered_set>

namespace XER::ModelsBuilder::GraphEngine {

void GraphOptimizer::PruneDeadNodes(GraphBase& graph) {
  // Build set of all consumed inputs across the graph
  std::unordered_set<std::string> consumed;
  for (auto& [id, node] : graph.Nodes())
    for (auto& inp : node.inputs) consumed.insert(inp);

  // Collect ids whose all outputs are unconsumed (dead nodes)
  std::vector<int64_t> to_remove;
  for (auto& [id, node] : graph.Nodes()) {
    if (node.outputs.empty()) continue;
    bool dead = true;
    for (auto& out : node.outputs)
      if (consumed.count(out)) { dead = false; break; }
    if (dead) to_remove.push_back(id);
  }
  for (int64_t id : to_remove) graph.RemoveNode(id);
}

void GraphOptimizer::Optimize(GraphBase& graph) {
  PruneDeadNodes(graph);
}

}  // namespace XER::ModelsBuilder::GraphEngine
