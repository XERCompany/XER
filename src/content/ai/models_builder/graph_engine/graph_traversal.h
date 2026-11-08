#pragma once

#include "graph_base.h"
#include <functional>
#include <vector>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief BFS and DFS traversal utilities for GraphBase
class GraphTraversal {
 public:
  using Visitor = std::function<void(const GraphNode&)>;

  /// Breadth-first traversal from root ids (or all roots if empty)
  static void BFS(const GraphBase& graph,
                   const Visitor& visitor,
                   std::vector<int64_t> start_ids = {});

  /// Depth-first traversal
  static void DFS(const GraphBase& graph,
                   const Visitor& visitor,
                   std::vector<int64_t> start_ids = {});
};

}  // namespace XER::ModelsBuilder::GraphEngine
