#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace XER::ModelsBuilder::GraphEngine {

using AttrValue = std::variant<float, int64_t, std::string,
                                std::vector<float>, std::vector<int64_t>>;

/// @brief A single node in the computational graph
struct GraphNode {
  int64_t     id{-1};
  std::string op_type;
  std::string name;
  std::vector<std::string> inputs;   ///< Tensor names consumed
  std::vector<std::string> outputs;  ///< Tensor names produced
  std::unordered_map<std::string, AttrValue> attrs;

  GraphNode() = default;
  GraphNode(int64_t id, std::string op_type, std::string name)
      : id(id), op_type(std::move(op_type)), name(std::move(name)) {}

  bool IsValid() const { return id >= 0 && !op_type.empty(); }
};

}  // namespace XER::ModelsBuilder::GraphEngine
