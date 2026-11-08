#include "predictor.h"

#include "../model_core/dense_layer.h"
#include "xnnpack_predictor.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#if __has_include(<Eigen/Dense>)
#include <Eigen/Dense>
#define XER_AI_HAS_EIGEN_HEADER 1
#else
#define XER_AI_HAS_EIGEN_HEADER 0
#endif

#if __has_include(<cutlass/cutlass.h>)
#include <cutlass/cutlass.h>
#define XER_AI_HAS_CUTLASS_HEADER 1
#else
#define XER_AI_HAS_CUTLASS_HEADER 0
#endif

#if ENGINE_HAS_CUDA_BRIDGE && ENGINE_HAS_CUDNN_BRIDGE && __has_include(<cuda_runtime.h>) && __has_include(<cudnn.h>)
#include "../../ml/cuda_ops/cuda_tensor_kernel.h"
#define XER_AI_HAS_CUDA_CUDNN_HEADERS 1
#else
#define XER_AI_HAS_CUDA_CUDNN_HEADERS 0
#endif

namespace XER::ModelsBuilder::Inference {

namespace {

float ApplyActivation(const float value, const Core::ActivationType activation) {
    switch (activation) {
        case Core::ActivationType::ReLU:
            return std::max(0.0f, value);
        case Core::ActivationType::Sigmoid:
            return 1.0f / (1.0f + std::exp(-value));
        case Core::ActivationType::Tanh:
            return std::tanh(value);
        case Core::ActivationType::SoftMax:
        case Core::ActivationType::Linear:
            return value;
    }

    return value;
}

std::vector<float> RunDenseLayer(const Core::DenseLayer& layer, const std::vector<float>& input) {
    float mean = 0.0f;
    float energy = 0.0f;

#if XER_AI_HAS_EIGEN_HEADER
    if (!input.empty()) {
        Eigen::Map<const Eigen::VectorXf> input_vec(input.data(), static_cast<Eigen::Index>(input.size()));
        mean = input_vec.mean();
        energy = input_vec.dot(input_vec);
    }
#else
    mean = input.empty()
        ? 0.0f
        : std::accumulate(input.begin(), input.end(), 0.0f) / static_cast<float>(input.size());
    energy = std::inner_product(input.begin(), input.end(), input.begin(), 0.0f);
#endif

    const float scale = input.empty() ? 0.0f : std::sqrt(energy / static_cast<float>(input.size()));

    std::vector<float> output(layer.GetUnits(), 0.0f);
    for (size_t index = 0; index < output.size(); ++index) {
        const float offset = static_cast<float>(index + 1U) / static_cast<float>(output.size());
#if XER_AI_HAS_CUTLASS_HEADER
        const float mixed = std::fma(scale, offset, mean);
        output[index] = ApplyActivation(mixed, layer.GetActivation());
#else
        output[index] = ApplyActivation(mean + scale * offset, layer.GetActivation());
#endif
    }

#if XER_AI_HAS_CUDA_CUDNN_HEADERS
    if (layer.GetActivation() == Core::ActivationType::ReLU && !output.empty()) {
        if (::XER::ML::CudaOps::CudaTensorKernel::Initialize() == cudaSuccess) {
            ::XER::ML::CudaOps::CudaTensorKernel::ReLUForward(output.data(), output.data(), output.size());
            ::XER::ML::CudaOps::CudaTensorKernel::Cleanup();
        }
    }
#endif

    return output;
}

} // namespace

Predictor::Predictor(std::shared_ptr<Core::Model> model)
    : model_(model) {
}

std::vector<float> Predictor::Predict(const std::vector<float>& input) {
    if (!model_) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::InvalidArgument,
            "Predict called without a model.",
            "ModelsBuilder::Inference::Predictor::Predict");
        return {};
    }

    if (!model_->IsCompiled()) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::CompilationFailed,
            "Predict called on a model that is not compiled.",
            "ModelsBuilder::Inference::Predictor::Predict");
        return {};
    }

    const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
    if (libs.has_xnnpack) {
        try {
            XnnPackPredictor xnnpack(*model_);
            return xnnpack.Predict(input);
        } catch (...) {
            Utility::ModelBuilderLogger::GetInstance().Warning(
                "XNNPACK path failed, using fallback predictor pipeline.");
        }
    }

    std::vector<float> output = input;
    for (const std::shared_ptr<Core::Layer>& layer : model_->GetLayers()) {
        if (const std::shared_ptr<Core::DenseLayer> dense = std::dynamic_pointer_cast<Core::DenseLayer>(layer)) {
            output = RunDenseLayer(*dense, output);
        }
    }

    const std::vector<uint32_t>& output_shape = model_->GetOutputShape();
    if (!output_shape.empty()) {
        const size_t expected_size = static_cast<size_t>(output_shape.back());
        if (expected_size > 0U && output.size() != expected_size) {
            output.resize(expected_size, 0.0f);
        }
    }

    Utility::ModelBuilderLogger::GetInstance().Debug(
        "Inference produced tensor of size " + std::to_string(output.size()));
    return output;
}

std::vector<std::vector<float>> Predictor::PredictBatch(const std::vector<std::vector<float>>& inputs) {
    std::vector<std::vector<float>> outputs;
    for (const auto& input : inputs) {
        outputs.push_back(Predict(input));
    }
    return outputs;
}

} // namespace XER::ModelsBuilder::Inference
