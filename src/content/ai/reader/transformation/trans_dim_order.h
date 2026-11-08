#pragma once

#include "../../models_builder/model_core/model.h"
#include <memory>

namespace XER::ModelsBuilder::Reader::Transform {

/// @brief Reorders tensor dimensions between NCHW ↔ NHWC layout
///
/// ONNX uses NCHW; some frameworks export NHWC.
/// This pass inserts Transpose layers where needed.
class TransDimOrder {
 public:
  enum class Layout { NCHW, NHWC };

  /// Reorder model dimensions from source to target layout
  /// @return Modified model copy
  std::shared_ptr<Core::Model> Apply(std::shared_ptr<Core::Model> model,
                                      Layout from_layout,
                                      Layout to_layout);

 private:
  static std::shared_ptr<Core::Layer> MakeTransposeLayer(
      Layout from, Layout to);
};

}  // namespace XER::ModelsBuilder::Reader::Transform
