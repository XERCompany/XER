#include "graph_memory_allocator.h"

namespace XER::ModelsBuilder::GraphEngine {

std::unordered_map<std::string, GraphMemoryAllocator::TensorAllocation>
GraphMemoryAllocator::Plan(
    const GraphBase& graph,
    const std::unordered_map<std::string, size_t>& tensor_sizes,
    size_t elem_bytes) {
  std::unordered_map<std::string, TensorAllocation> plan;
  size_t cursor = 0;

  auto order = graph.TopologicalSort();
  for (int64_t id : order) {
    const GraphNode* node = graph.GetNode(id);
    if (!node) continue;
    for (auto& out : node->outputs) {
      auto it = tensor_sizes.find(out);
      size_t n = (it != tensor_sizes.end()) ? it->second : 1024;
      TensorAllocation alloc;
      alloc.offset = cursor;
      alloc.size   = n * elem_bytes;
      plan[out]    = alloc;
      cursor      += alloc.size;
      // 16-byte alignment
      cursor = (cursor + 15) & ~static_cast<size_t>(15);
    }
  }
  return plan;
}

size_t GraphMemoryAllocator::ArenaSize(
    const std::unordered_map<std::string, TensorAllocation>& plan) {
  size_t max = 0;
  for (auto& [name, alloc] : plan) {
    size_t end = alloc.offset + alloc.size;
    if (end > max) max = end;
  }
  return max;
}

}  // namespace XER::ModelsBuilder::GraphEngine
