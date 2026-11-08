#pragma once

#include "../model_core/model.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace XER::ModelsBuilder::Operations {

enum class QuantizationMode : uint8_t {
  None = 0,
  Float16 = 1,
  Int8 = 2
};

struct QuantizationReport {
  QuantizationMode mode = QuantizationMode::None;
  size_t original_bytes_estimate = 0U;
  size_t quantized_bytes_estimate = 0U;
};

class ModelQuantizer {
 public:
  std::shared_ptr<Core::Model> Quantize(const Core::Model& model,
                                        QuantizationMode mode,
                                        QuantizationReport* report) const;
};

}  // namespace XER::ModelsBuilder::Operations
