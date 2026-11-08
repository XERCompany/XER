#pragma once

#include <cstdint>
#include <string>

namespace XER::ModelsBuilder::GraphEngine {

/// @brief Represents a directed edge connecting two graph nodes via tensor name
struct EdgeInfo {
  int64_t     src_node_id{-1};
  int64_t     dst_node_id{-1};
  int32_t     src_port{0};
  int32_t     dst_port{0};
  std::string tensor_name;
};

}  // namespace XER::ModelsBuilder::GraphEngine
