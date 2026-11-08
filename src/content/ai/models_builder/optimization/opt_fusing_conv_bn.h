#pragma once

#include "../graph_engine/graph_base.h"

namespace XER::ModelsBuilder::Optimization {

/// @brief Fuses consecutive Conv2D + BatchNorm into a single Conv2D node
class OptFusingConvBn {
 public:
  static void Apply(GraphEngine::GraphBase& graph);
};

}  // namespace XER::ModelsBuilder::Optimization
