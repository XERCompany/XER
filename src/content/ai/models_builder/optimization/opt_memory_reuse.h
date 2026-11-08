#pragma once

#include "../graph_engine/graph_base.h"
#include <unordered_map>
#include <string>

namespace XER::ModelsBuilder::Optimization {

/// @brief Plans in-place tensor memory reuse across graph nodes
class OptMemoryReuse {
 public:
  struct ReuseEntry {
    std::string reuse_from;  ///< Tensor name being reused
    bool        in_place{false};
  };

  /// @return Map from output tensor → reuse plan
  static std::unordered_map<std::string, ReuseEntry>
  Plan(const GraphEngine::GraphBase& graph);
};

}  // namespace XER::ModelsBuilder::Optimization
