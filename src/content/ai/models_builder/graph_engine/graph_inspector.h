#pragma once

#include "graph_base.h"
#include <string>
#include <vector>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Introspection queries for GraphBase
class GraphInspector {
 public:
  /// Return all nodes with a given op_type
  static std::vector<int64_t> FindByOpType(const GraphBase& graph,
                                             const std::string& op_type);

  /// Return ids of nodes that produce a given tensor name
  static int64_t FindProducer(const GraphBase& graph,
                               const std::string& tensor_name);

  /// Return ids of nodes that consume a given tensor name
  static std::vector<int64_t> FindConsumers(const GraphBase& graph,
                                              const std::string& tensor_name);

  /// Return all output tensor names of the entire graph (not consumed by any node)
  static std::vector<std::string> GraphOutputs(const GraphBase& graph);
};

}  // namespace XER::ModelsBuilder::GraphEngine
