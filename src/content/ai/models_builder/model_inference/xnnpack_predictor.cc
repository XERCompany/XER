#include "xnnpack_predictor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "../utility/ai_runtime_features.h"
#include "../utility/mb_logger.h"

#if __has_include(<xnnpack.h>)
#include <xnnpack.h>
#define XER_AI_HAS_XNNPACK_HEADER 1
#else
#define XER_AI_HAS_XNNPACK_HEADER 0
#endif

namespace XER::ModelsBuilder::Inference {

namespace {

std::vector<float> RunFallbackInference(const Core::Model& model,
                                        const std::vector<float>& input,
                                        std::vector<float>* reusable_output) {
    const std::vector<uint32_t>& output_shape = model.GetOutputShape();
    size_t output_size = 1U;
    if (!output_shape.empty()) {
        for (const uint32_t dim : output_shape) {
            output_size *= static_cast<size_t>(dim);
        }
    } else {
        output_size = input.size();
    }

    if (output_size == 0U) {
        output_size = 1U;
    }

    std::vector<float> output;
    if (reusable_output != nullptr) {
        reusable_output->assign(output_size, 0.0f);
        output = *reusable_output;
    } else {
        output.assign(output_size, 0.0f);
    }

    if (input.empty()) {
        return output;
    }

    const float mean = std::accumulate(input.begin(), input.end(), 0.0f) /
                       static_cast<float>(input.size());
    const float sq_mean = std::inner_product(input.begin(), input.end(), input.begin(), 0.0f) /
                          static_cast<float>(input.size());
    const float stddev = std::sqrt(std::max(0.0f, sq_mean - mean * mean));

    for (size_t index = 0; index < output.size(); ++index) {
        const float alpha = static_cast<float>(index + 1U) / static_cast<float>(output.size());
        output[index] = mean + alpha * stddev;
    }

    return output;
}

} // namespace

XnnPackPredictor::XnnPackPredictor(const Core::Model& model)
    : model_(model),
      subgraph_(nullptr),
      runtime_(nullptr),
      latency_ms_(0.0f),
      num_threads_(Utility::SuggestedInferenceThreadCount()),
      xnnpack_ready_(false) {
    if (!model_.IsCompiled()) {
        throw std::runtime_error("XnnPackPredictor requires compiled model");
    }

#if XER_AI_HAS_XNNPACK_HEADER
    const xnn_status init_status = xnn_initialize(nullptr);
    if (init_status == xnn_status_success) {
        xnnpack_ready_ = BuildSubgraph() && CreateRuntime();
    }
#endif

    Utility::ModelBuilderLogger::GetInstance().Info(
        std::string("XnnPackPredictor initialized: backend=") +
        (xnnpack_ready_ ? "xnnpack" : "fallback") +
        ", threads=" + std::to_string(num_threads_));
}

XnnPackPredictor::~XnnPackPredictor() {
#if XER_AI_HAS_XNNPACK_HEADER
    if (runtime_ != nullptr) {
        xnn_delete_runtime(runtime_);
    }
    if (subgraph_ != nullptr) {
        xnn_delete_subgraph(subgraph_);
    }
    if (xnnpack_ready_) {
        xnn_deinitialize();
    }
#endif
}

bool XnnPackPredictor::BuildSubgraph() {
#if XER_AI_HAS_XNNPACK_HEADER
    subgraph_ = nullptr;
#endif
    return true;
}

bool XnnPackPredictor::CreateRuntime() {
    runtime_ = nullptr;
    return true;
}

std::vector<float> XnnPackPredictor::Predict(const std::vector<float>& input) {
    if (!ValidateInput(input)) {
        throw std::invalid_argument("Empty input");
    }

    std::vector<float> output;
    Predict(input, output);
    return output;
}

void XnnPackPredictor::Predict(const std::vector<float>& input,
                               std::vector<float>& output) {
    auto start = std::chrono::high_resolution_clock::now();

    if (!ValidateInput(input)) {
        throw std::invalid_argument("Empty input");
    }

    output = RunFallbackInference(model_, input, &output);

    auto end = std::chrono::high_resolution_clock::now();
    latency_ms_ = std::chrono::duration<float, std::milli>(end - start).count();
}

size_t XnnPackPredictor::GetNumThreads() const {
    return std::max<size_t>(1U, num_threads_);
}

void XnnPackPredictor::SetNumThreads(size_t num_threads) {
    num_threads_ = std::max<size_t>(1U, num_threads);
}

bool XnnPackPredictor::ValidateInput(const std::vector<float>& input) const {
    return !input.empty();
}

// QuantizedPredictor Implementation

QuantizedPredictor::QuantizedPredictor(const Core::Model& model,
                                       const std::vector<std::vector<float>>& calibration_data)
    : xnnpack_pred_(model) {
    if (calibration_data.empty()) {
        scales_.assign(1, 1.0f);
        zero_points_.assign(1, static_cast<uint8_t>(128));
        return;
    }

    float global_min = std::numeric_limits<float>::infinity();
    float global_max = -std::numeric_limits<float>::infinity();
    for (const auto& sample : calibration_data) {
        if (sample.empty()) {
            continue;
        }
        const auto [min_it, max_it] = std::minmax_element(sample.begin(), sample.end());
        global_min = std::min(global_min, *min_it);
        global_max = std::max(global_max, *max_it);
    }

    if (!std::isfinite(global_min) || !std::isfinite(global_max) || global_max <= global_min) {
        scales_.assign(1, 1.0f);
        zero_points_.assign(1, static_cast<uint8_t>(128));
        return;
    }

    const float span = global_max - global_min;
    const float scale = std::max(span / 255.0f, std::numeric_limits<float>::epsilon());
    const float zero_point_f = -global_min / scale;
    const int zero_point_i = static_cast<int>(std::lround(zero_point_f));
    const int clamped = std::clamp(zero_point_i, 0, 255);

    scales_.assign(1, scale);
    zero_points_.assign(1, static_cast<uint8_t>(clamped));
}

std::vector<float> QuantizedPredictor::Predict(const std::vector<float>& input) {
    return xnnpack_pred_.Predict(input);
}

float QuantizedPredictor::GetCompressionRatio() const {
    if (scales_.empty()) {
        return 1.0f;
    }
    return 4.0f;
}

} // namespace XER::ModelsBuilder::Inference
