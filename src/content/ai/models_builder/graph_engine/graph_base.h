#pragma once

#include "graph_node.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Base class for the computational graph — stores nodes + edges
class GraphBase {
 public:
  virtual ~GraphBase() = default;

  int64_t AddNode(GraphNode node);
  GraphNode* GetNode(int64_t id);
  const GraphNode* GetNode(int64_t id) const;
  bool HasNode(int64_t id) const;
  void RemoveNode(int64_t id);

  size_t NodeCount() const { return nodes_.size(); }
  const std::unordered_map<int64_t, GraphNode>& Nodes() const { return nodes_; }

  /// Topological sort; returns node ids in execution order
  /// @throws std::runtime_error if graph contains a cycle
  std::vector<int64_t> TopologicalSort() const;

  void Clear();

 protected:
  std::unordered_map<int64_t, GraphNode> nodes_;
  int64_t next_id_{0};
};

}  // namespace XER::ModelsBuilder::GraphEngine
