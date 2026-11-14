#pragma once

#include "../../models_builder/model_core/model.h"
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Schema {

/// @brief Validates an XER Core::Model for correctness
///
/// Checks: layer count > 0, input/output shapes consistent,
/// no null layers, no unsupported layer types.
class SchemaValidator {
 public:
  struct ValidationResult {
    bool                     valid{true};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
  };

  /// Run full validation on a loaded model
  ValidationResult Validate(const Core::Model& model) const;

  /// Check that all layer names are non-empty
  bool CheckLayerNames(const Core::Model& model,
                       std::vector<std::string>& errors) const;

  /// Check that the model has at least one layer
  bool CheckNonEmpty(const Core::Model& model,
                     std::vector<std::string>& errors) const;

 private:
  /// Weight/bias/shape consistency checks; fills warnings and errors.
  void CheckWeightsAndShapes(const Core::Model& model,
                             ValidationResult& result) const;
};

}  // namespace XER::ModelsBuilder::Reader::Schema
