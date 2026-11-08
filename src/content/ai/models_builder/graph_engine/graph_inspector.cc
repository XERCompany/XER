#include "graph_inspector.h"

#include <unordered_set>

namespace XER::ModelsBuilder::GraphEngine {

std::vector<int64_t> GraphInspector::FindByOpType(
    const GraphBase& graph, const std::string& op_type) {
  std::vector<int64_t> result;
  for (auto& [id, node] : graph.Nodes())
    if (node.op_type == op_type) result.push_back(id);
  return result;
}

int64_t GraphInspector::FindProducer(const GraphBase& graph,
                                      const std::string& tensor_name) {
  for (auto& [id, node] : graph.Nodes())
    for (auto& out : node.outputs)
      if (out == tensor_name) return id;
  return -1;
}

std::vector<int64_t> GraphInspector::FindConsumers(
    const GraphBase& graph, const std::string& tensor_name) {
  std::vector<int64_t> result;
  for (auto& [id, node] : graph.Nodes())
    for (auto& inp : node.inputs)
      if (inp == tensor_name) { result.push_back(id); break; }
  return result;
}

std::vector<std::string> GraphInspector::GraphOutputs(
    const GraphBase& graph) {
  // All produced tensors not consumed anywhere
  std::unordered_set<std::string> consumed;
  for (auto& [id, node] : graph.Nodes())
    for (auto& inp : node.inputs) consumed.insert(inp);

  std::vector<std::string> outputs;
  for (auto& [id, node] : graph.Nodes())
    for (auto& out : node.outputs)
      if (!consumed.count(out)) outputs.push_back(out);
  return outputs;
}

}  // namespace XER::ModelsBuilder::GraphEngine
