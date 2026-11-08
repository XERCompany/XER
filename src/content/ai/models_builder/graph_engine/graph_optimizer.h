#pragma once

#include "graph_base.h"
#include <memory>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Performs constant folding and dead node pruning on the graph
class GraphOptimizer {
 public:
  /// Remove dead nodes (nodes whose outputs are not consumed)
  static void PruneDeadNodes(GraphBase& graph);

  /// Run all optimization passes
  static void Optimize(GraphBase& graph);
};

}  // namespace XER::ModelsBuilder::GraphEngine
