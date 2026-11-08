#pragma once

#include "graph_base.h"
#include <string>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Renders a graph as Graphviz DOT format for debugging
class GraphDebugVisualizer {
 public:
  /// @return DOT string representing the graph
  static std::string ToDot(const GraphBase& graph,
                             const std::string& graph_name = "xer_graph");

  /// Write DOT file to disk
  /// @return true on success
  static bool WriteDotFile(const GraphBase& graph,
                             const std::string& filepath,
                             const std::string& graph_name = "xer_graph");
};

}  // namespace XER::ModelsBuilder::GraphEngine
