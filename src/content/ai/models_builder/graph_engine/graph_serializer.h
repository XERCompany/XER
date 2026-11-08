#pragma once

#include "graph_base.h"
#include <string>
#include <vector>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Serializes/deserializes a GraphBase to/from FlatBuffers binary
class GraphSerializer {
 public:
  /// Serialize to FlatBuffers byte vector
  static std::vector<uint8_t> Serialize(const GraphBase& graph);

  /// Deserialize; adds nodes into provided GraphBase
  /// @return false if data is malformed
  static bool Deserialize(const uint8_t* data, size_t size,
                           GraphBase& out_graph);
};

}  // namespace XER::ModelsBuilder::GraphEngine
