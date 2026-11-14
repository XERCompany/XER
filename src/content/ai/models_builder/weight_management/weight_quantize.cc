#include "weight_quantize.h"

#include "../utility/ai_runtime_features.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#if __has_include(<fp16.h>)
#include <fp16.h>
#define XER_WQ_HAS_FP16 1
#else
#define XER_WQ_HAS_FP16 0
#endif

namespace XER::ModelsBuilder::Weights {

QuantizationResult WeightQuantizer::Quantize(WeightTensor& tensor,
                                              QuantizeMode mode) const {
    QuantizationResult result;
    result.tensor_name = tensor.name;
    result.mode = mode;
    result.original_bytes = tensor.values.size() * sizeof(float);

    if (mode == QuantizeMode::None || tensor.values.empty()) {
        result.quantized_bytes = result.original_bytes;
        return result;
    }

    if (mode == QuantizeMode::Float16) {
#if XER_WQ_HAS_FP16
        // Real IEEE-754 half round-trip via the fp16 library: value -> fp16
        // bits -> fp32 gives the exact quantization error of fp16 storage.
        for (auto& v : tensor.values) {
            const uint16_t bits = fp16_ieee_from_fp32_value(v);
            const float dequantized = fp16_ieee_to_fp32_value(bits);
            result.max_error = std::max(result.max_error, std::abs(v - dequantized));
            v = dequantized;
        }
#else
        // FP16 simulation fallback: round to 3 significant decimal digits
        for (auto& v : tensor.values) {
            // Emulate fp16 range clamp (65504)
            v = std::max(-65504.0f, std::min(65504.0f, v));
            // Round to half-precision magnitude (11-bit mantissa → ~3 decimals)
            float rounded = std::round(v * 1024.0f) / 1024.0f;
            result.max_error = std::max(result.max_error, std::abs(v - rounded));
            v = rounded;
        }
#endif
        result.quantized_bytes = tensor.values.size() * 2;  // 16-bit
        result.scale = 1.0f;
        return result;
    }

    if (mode == QuantizeMode::Int8) {
        float vmin = *std::min_element(tensor.values.begin(), tensor.values.end());
        float vmax = *std::max_element(tensor.values.begin(), tensor.values.end());
        float range = vmax - vmin;
        if (range < 1e-9f) range = 1.0f;

        result.scale = range / 255.0f;
        result.zero_point = -vmin / result.scale;

        for (auto& v : tensor.values) {
            float q = std::round((v / result.scale) + result.zero_point);
            q = std::max(0.0f, std::min(255.0f, q));
            float dq = (q - result.zero_point) * result.scale;
            result.max_error = std::max(result.max_error, std::abs(v - dq));
            v = dq;  // store dequantized in float tensor
        }
        result.quantized_bytes = tensor.values.size() * 1;  // 8-bit equivalent
    }

    return result;
}

std::vector<QuantizationResult> WeightQuantizer::QuantizeAll(
    std::vector<WeightTensor>& tensors, QuantizeMode mode) const {
    std::vector<QuantizationResult> results;
    results.reserve(tensors.size());
    for (auto& t : tensors) {
        results.push_back(Quantize(t, mode));
    }
    return results;
}

void WeightQuantizer::DequantizeInt8(WeightTensor& tensor,
                                      float scale, float zero_point) const {
    for (auto& v : tensor.values) {
        // v already represents int8 quantized value stored as float
        float q = std::round(v);
        q = std::max(0.0f, std::min(255.0f, q));
        v = (q - zero_point) * scale;
    }
}

std::string WeightQuantizer::ModeName(QuantizeMode mode) {
    switch (mode) {
        case QuantizeMode::Float16: return "Float16";
        case QuantizeMode::Int8:    return "Int8";
        default:                    return "None";
    }
}

}  // namespace XER::ModelsBuilder::Weights
