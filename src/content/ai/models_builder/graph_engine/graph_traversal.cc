#include "graph_traversal.h"

#include <queue>
#include <stack>
#include <unordered_set>

namespace XER::ModelsBuilder::GraphEngine {

// Helper: build producer map (output tensor → node id)
static std::unordered_map<std::string, int64_t>
BuildProducer(const GraphBase& graph) {
  std::unordered_map<std::string, int64_t> p;
  for (auto& [id, node] : graph.Nodes())
    for (auto& out : node.outputs) p[out] = id;
  return p;
}

// Helper: build adjacency list (id → successor ids)
static std::unordered_map<int64_t, std::vector<int64_t>>
BuildAdj(const GraphBase& graph) {
  auto producer = BuildProducer(graph);
  std::unordered_map<int64_t, std::vector<int64_t>> adj;
  for (auto& [id, node] : graph.Nodes()) {
    for (auto& inp : node.inputs) {
      auto it = producer.find(inp);
      if (it != producer.end())
        adj[it->second].push_back(id);
    }
  }
  return adj;
}

// Helper: find root ids (no incoming edges)
static std::vector<int64_t> FindRoots(const GraphBase& graph) {
  auto producer = BuildProducer(graph);
  std::unordered_set<int64_t> has_parent;
  for (auto& [id, node] : graph.Nodes())
    for (auto& inp : node.inputs)
      if (producer.count(inp)) has_parent.insert(id);

  std::vector<int64_t> roots;
  for (auto& [id, _] : graph.Nodes())
    if (!has_parent.count(id)) roots.push_back(id);
  return roots;
}

void GraphTraversal::BFS(const GraphBase& graph, const Visitor& visitor,
                          std::vector<int64_t> start_ids) {
  if (start_ids.empty()) start_ids = FindRoots(graph);
  auto adj = BuildAdj(graph);
  std::unordered_set<int64_t> visited;
  std::queue<int64_t> q;
  for (int64_t id : start_ids) { q.push(id); visited.insert(id); }
  while (!q.empty()) {
    int64_t id = q.front(); q.pop();
    const GraphNode* n = graph.GetNode(id);
    if (n) visitor(*n);
    for (int64_t s : adj[id])
      if (!visited.count(s)) { visited.insert(s); q.push(s); }
  }
}

void GraphTraversal::DFS(const GraphBase& graph, const Visitor& visitor,
                          std::vector<int64_t> start_ids) {
  if (start_ids.empty()) start_ids = FindRoots(graph);
  auto adj = BuildAdj(graph);
  std::unordered_set<int64_t> visited;
  std::stack<int64_t> stk;
  for (int64_t id : start_ids) stk.push(id);
  while (!stk.empty()) {
    int64_t id = stk.top(); stk.pop();
    if (visited.count(id)) continue;
    visited.insert(id);
    const GraphNode* n = graph.GetNode(id);
    if (n) visitor(*n);
    for (int64_t s : adj[id]) stk.push(s);
  }
}

}  // namespace XER::ModelsBuilder::GraphEngine
