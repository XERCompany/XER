#include "opt_fusing_conv_bn.h"

#include <vector>
#include <unordered_map>

namespace XER::ModelsBuilder::Optimization {

void OptFusingConvBn::Apply(GraphEngine::GraphBase& graph) {
  // Find Conv → BN pairs and merge BN into Conv
  std::unordered_map<std::string, int64_t> producer;
  for (auto& [id, node] : graph.Nodes())
    for (auto& out : node.outputs) producer[out] = id;

  std::vector<int64_t> bn_to_remove;
  for (auto& [id, node] : graph.Nodes()) {
    if (node.op_type != "BatchNormalization") continue;
    if (node.inputs.empty()) continue;
    auto it = producer.find(node.inputs[0]);
    if (it == producer.end()) continue;
    auto* conv = graph.GetNode(it->second);
    if (!conv || conv->op_type != "Conv") continue;
    // Absorb BN outputs into Conv outputs
    conv->outputs = node.outputs;
    bn_to_remove.push_back(id);
  }
  for (int64_t id : bn_to_remove) graph.RemoveNode(id);
}

}  // namespace XER::ModelsBuilder::Optimization
