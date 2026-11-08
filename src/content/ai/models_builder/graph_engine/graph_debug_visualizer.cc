#include "graph_debug_visualizer.h"

#include <fstream>
#include <sstream>

namespace XER::ModelsBuilder::GraphEngine {

std::string GraphDebugVisualizer::ToDot(const GraphBase& graph,
                                         const std::string& graph_name) {
  // Build producer map
  std::unordered_map<std::string, int64_t> producer;
  for (auto& [id, node] : graph.Nodes())
    for (auto& out : node.outputs) producer[out] = id;

  std::ostringstream dot;
  dot << "digraph " << graph_name << " {\n";
  dot << "  rankdir=LR;\n";
  for (auto& [id, node] : graph.Nodes()) {
    dot << "  n" << id << " [label=\"" << node.op_type;
    if (!node.name.empty()) dot << "\\n" << node.name;
    dot << "\"];\n";
    for (auto& inp : node.inputs) {
      auto it = producer.find(inp);
      if (it != producer.end())
        dot << "  n" << it->second << " -> n" << id
            << " [label=\"" << inp << "\"];\n";
    }
  }
  dot << "}\n";
  return dot.str();
}

bool GraphDebugVisualizer::WriteDotFile(const GraphBase& graph,
                                          const std::string& filepath,
                                          const std::string& graph_name) {
  std::ofstream f(filepath);
  if (!f.is_open()) return false;
  f << ToDot(graph, graph_name);
  return f.good();
}

}  // namespace XER::ModelsBuilder::GraphEngine
