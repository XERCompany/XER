#pragma once

#include "../graph_engine/graph_base.h"

namespace XER::ModelsBuilder::Optimization {

/// @brief Removes dead/unreachable nodes from the computation graph
class OptDeadNodeRemover {
 public:
  static void Apply(GraphEngine::GraphBase& graph);
};

}  // namespace XER::ModelsBuilder::Optimization
