#include "graph_base.h"

#include <stdexcept>
#include <unordered_set>

namespace XER::ModelsBuilder::GraphEngine {

int64_t GraphBase::AddNode(GraphNode node) {
  if (node.id < 0) node.id = next_id_++;
  else if (node.id >= next_id_) next_id_ = node.id + 1;
  int64_t id = node.id;
  nodes_[id] = std::move(node);
  return id;
}

GraphNode* GraphBase::GetNode(int64_t id) {
  auto it = nodes_.find(id);
  return it != nodes_.end() ? &it->second : nullptr;
}

const GraphNode* GraphBase::GetNode(int64_t id) const {
  auto it = nodes_.find(id);
  return it != nodes_.end() ? &it->second : nullptr;
}

bool GraphBase::HasNode(int64_t id) const { return nodes_.count(id) > 0; }

void GraphBase::RemoveNode(int64_t id) { nodes_.erase(id); }

void GraphBase::Clear() {
  nodes_.clear();
  next_id_ = 0;
}

std::vector<int64_t> GraphBase::TopologicalSort() const {
  // Build producer map: tensor_name → node_id
  std::unordered_map<std::string, int64_t> producer;
  for (auto& [id, node] : nodes_)
    for (auto& out : node.outputs) producer[out] = id;

  // Build adjacency + in-degree
  std::unordered_map<int64_t, std::vector<int64_t>> succ;
  std::unordered_map<int64_t, int> indegree;
  for (auto& [id, node] : nodes_) {
    if (!indegree.count(id)) indegree[id] = 0;
    for (auto& inp : node.inputs) {
      auto it = producer.find(inp);
      if (it != producer.end()) {
        succ[it->second].push_back(id);
        indegree[id]++;
      }
    }
  }

  // Kahn's algorithm
  std::vector<int64_t> queue, result;
  for (auto& [id, deg] : indegree)
    if (deg == 0) queue.push_back(id);

  while (!queue.empty()) {
    int64_t n = queue.back(); queue.pop_back();
    result.push_back(n);
    for (int64_t s : succ[n])
      if (--indegree[s] == 0) queue.push_back(s);
  }

  if (result.size() != nodes_.size())
    throw std::runtime_error("Graph cycle detected in TopologicalSort");
  return result;
}

}  // namespace XER::ModelsBuilder::GraphEngine
