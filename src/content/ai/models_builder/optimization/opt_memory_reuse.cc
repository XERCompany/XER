#include "opt_memory_reuse.h"

#include <unordered_set>

namespace XER::ModelsBuilder::Optimization {

std::unordered_map<std::string, OptMemoryReuse::ReuseEntry>
OptMemoryReuse::Plan(const GraphEngine::GraphBase& graph) {
  std::unordered_map<std::string, ReuseEntry> plan;

  // For ops like Relu/Sigmoid that are element-wise, output can reuse input
  static const std::unordered_set<std::string> kInPlace = {
    "Relu", "Sigmoid", "Tanh", "Gelu", "LeakyRelu"
  };

  for (auto& [id, node] : graph.Nodes()) {
    if (!kInPlace.count(node.op_type)) continue;
    if (node.inputs.empty() || node.outputs.empty()) continue;
    ReuseEntry e;
    e.reuse_from = node.inputs[0];
    e.in_place   = true;
    plan[node.outputs[0]] = e;
  }
  return plan;
}

}  // namespace XER::ModelsBuilder::Optimization
