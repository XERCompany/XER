#pragma once

#include "graph_base.h"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Plans tensor memory allocation across nodes (arena planning)
class GraphMemoryAllocator {
 public:
  struct TensorAllocation {
    size_t offset{0};   ///< Byte offset in the tensor arena
    size_t size{0};     ///< Byte size
    bool   in_place{false};
  };

  /// Compute allocations for all tensors in topological order
  /// @param elem_bytes  Bytes per element (default 4 for float32)
  static std::unordered_map<std::string, TensorAllocation>
  Plan(const GraphBase& graph,
       const std::unordered_map<std::string, size_t>& tensor_sizes,
       size_t elem_bytes = 4);

  /// Total arena size from a completed plan
  static size_t ArenaSize(
      const std::unordered_map<std::string, TensorAllocation>& plan);
};

}  // namespace XER::ModelsBuilder::GraphEngine
