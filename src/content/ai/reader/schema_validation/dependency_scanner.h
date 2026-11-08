#pragma once

#include "../../models_builder/model_core/model.h"
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Schema {

/// @brief Checks model weight tensor dependencies (no dangling refs)
class DependencyScanner {
 public:
  struct ScanResult {
    bool                     ok{true};
    std::vector<std::string> missing;    ///< Referenced tensors not found
    std::vector<std::string> orphaned;   ///< Tensors produced but never consumed
  };

  ScanResult Scan(const Core::Model& model) const;
};

}  // namespace XER::ModelsBuilder::Reader::Schema
