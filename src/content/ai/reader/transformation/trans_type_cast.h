#pragma once

#include "../../models_builder/model_core/model.h"
#include <memory>

namespace XER::ModelsBuilder::Reader::Transform {

/// @brief Inserts explicit Cast layers where data type transitions occur
class TransTypeCast {
 public:
  /// Target compute dtype
  enum class DType { Float32, Float16, Int8 };

  /// Insert Cast layers at dtype boundaries
  std::shared_ptr<Core::Model> Apply(std::shared_ptr<Core::Model> model,
                                      DType target_dtype);
};

}  // namespace XER::ModelsBuilder::Reader::Transform
