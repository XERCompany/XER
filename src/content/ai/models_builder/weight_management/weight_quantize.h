#pragma once

#include "weight_initialization.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

enum class QuantizeMode : uint8_t {
    None     = 0,
    Float16  = 1,
    Int8     = 2,
};

struct QuantizationResult {
    std::string tensor_name;
    QuantizeMode mode = QuantizeMode::None;
    float scale = 1.0f;
    float zero_point = 0.0f;
    float max_error = 0.0f;
    size_t original_bytes = 0;
    size_t quantized_bytes = 0;
};

class WeightQuantizer {
public:
    // Quantize a single tensor in-place.  Returns stats.
    QuantizationResult Quantize(WeightTensor& tensor, QuantizeMode mode) const;

    // Quantize all tensors in a list.
    std::vector<QuantizationResult> QuantizeAll(std::vector<WeightTensor>& tensors,
                                                QuantizeMode mode) const;

    // Dequantize a tensor previously quantized to Int8 using stored scale/zp.
    void DequantizeInt8(WeightTensor& tensor,
                        float scale, float zero_point) const;

    static std::string ModeName(QuantizeMode mode);
};

}  // namespace XER::ModelsBuilder::Weights
