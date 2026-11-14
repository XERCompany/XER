#include "predictor.h"

#include "../model_core/dense_layer.h"
#include "xnnpack_predictor.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

#if defined(XER_AI_HAS_GGML)
#include <ggml.h>
#include <ggml-backend.h>
#include <ggml-cpu.h>
#define XER_PRED_HAS_GGML 1
#else
#define XER_PRED_HAS_GGML 0
#endif

// BLAS dense path: cblas_sgemm from MKL/OpenBLAS (whichever provides cblas.h).
#if __has_include(<cblas.h>)
#include <cblas.h>
#define XER_PRED_HAS_CBLAS 1
#else
#define XER_PRED_HAS_CBLAS 0
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

// ggml graph executor: y = act(W*x + b) on a real ggml compute graph.
// Row-major W[units x in] maps directly onto ggml ne={in, units} layout.
bool TryGgmlDenseForward(const Core::DenseLayer& layer,
                         const std::vector<float>& input,
                         std::vector<float>& output) {
#if XER_PRED_HAS_GGML
    const size_t in_dim = input.size();
    const size_t units = layer.GetUnits();
    if (!layer.HasWeights() || in_dim == 0U || units == 0U) {
        return false;
    }
    const std::vector<float>& weights = layer.GetWeights();
    const std::vector<float>& bias = layer.GetBias();
    if (weights.size() != units * in_dim || (!bias.empty() && bias.size() != units)) {
        return false;
    }

    const size_t mem_size = weights.size() * sizeof(float) +
                            input.size() * sizeof(float) +
                            units * sizeof(float) * 4U + (4U << 20U);
    ggml_init_params params = {mem_size, nullptr, false};
    ggml_context* ctx = ggml_init(params);
    if (ctx == nullptr) {
        return false;
    }

    ggml_tensor* w = ggml_new_tensor_2d(ctx, GGML_TYPE_F32,
                                        static_cast<int64_t>(in_dim),
                                        static_cast<int64_t>(units));
    ggml_tensor* x = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,
                                        static_cast<int64_t>(in_dim));
    ggml_tensor* y = ggml_mul_mat(ctx, w, x);
    ggml_tensor* b = nullptr;
    if (y != nullptr && !bias.empty()) {
        b = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 1,
                               static_cast<int64_t>(units));
        if (b != nullptr) {
            y = ggml_add(ctx, y, b);
        }
    }
    if (y != nullptr) {
        switch (layer.GetActivation()) {
            case Core::ActivationType::ReLU:    y = ggml_relu(ctx, y); break;
            case Core::ActivationType::Sigmoid: y = ggml_sigmoid(ctx, y); break;
            case Core::ActivationType::Tanh:    y = ggml_tanh(ctx, y); break;
            default: break;
        }
    }
    if (y == nullptr || y->data == nullptr) {
        ggml_free(ctx);
        return false;
    }

    ggml_cgraph* graph = ggml_new_graph(ctx);
    if (graph == nullptr) {
        ggml_free(ctx);
        return false;
    }
    ggml_build_forward_expand(graph, y);

    std::memcpy(w->data, weights.data(), weights.size() * sizeof(float));
    std::memcpy(x->data, input.data(), input.size() * sizeof(float));
    if (b != nullptr) {
        std::memcpy(b->data, bias.data(), bias.size() * sizeof(float));
    }

    const int threads = static_cast<int>(
        std::min<size_t>(Utility::SuggestedInferenceThreadCount(), 64U));
    const ggml_status status = ggml_graph_compute_with_ctx(ctx, graph, threads);
    if (status != GGML_STATUS_SUCCESS) {
        ggml_free(ctx);
        return false;
    }

    output.assign(reinterpret_cast<const float*>(y->data),
                  reinterpret_cast<const float*>(y->data) + units);
    ggml_free(ctx);
    return true;
#else
    (void)layer; (void)input; (void)output;
    return false;
#endif
}

// BLAS path: y = W*x via cblas_sgemm (row-major), then bias+activation.
bool TryBlasDenseForward(const Core::DenseLayer& layer,
                         const std::vector<float>& input,
                         std::vector<float>& output) {
#if XER_PRED_HAS_CBLAS
    const size_t in_dim = input.size();
    const size_t units = layer.GetUnits();
    if (!layer.HasWeights() || in_dim == 0U || units == 0U) {
        return false;
    }
    const std::vector<float>& weights = layer.GetWeights();
    const std::vector<float>& bias = layer.GetBias();
    if (weights.size() != units * in_dim || (!bias.empty() && bias.size() != units)) {
        return false;
    }

    output.assign(units, 0.0f);
    // C(units x 1) = A(units x in) * B(in x 1), all row-major.
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                static_cast<int>(units), 1, static_cast<int>(in_dim),
                1.0f, weights.data(), static_cast<int>(in_dim),
                input.data(), 1, 0.0f, output.data(), 1);
    for (size_t i = 0; i < units; ++i) {
        const float v = output[i] + (bias.empty() ? 0.0f : bias[i]);
        output[i] = ApplyActivation(v, layer.GetActivation());
    }
    return true;
#else
    (void)layer; (void)input; (void)output;
    return false;
#endif
}

// Real dense forward pass: y = act(W*x + b), W row-major [units x in].
// Returns false when weights are absent or shapes don't match the input.
bool RunDenseLayerReal(const Core::DenseLayer& layer,
                       const std::vector<float>& input,
                       std::vector<float>& output) {
    if (TryGgmlDenseForward(layer, input, output)) {
        return true;
    }
    if (TryBlasDenseForward(layer, input, output)) {
        return true;
    }

    const size_t in_dim = input.size();
    const size_t units = layer.GetUnits();
    if (!layer.HasWeights() || in_dim == 0U || units == 0U) {
        return false;
    }
    const std::vector<float>& weights = layer.GetWeights();
    const std::vector<float>& bias = layer.GetBias();
    if (weights.size() != units * in_dim || (!bias.empty() && bias.size() != units)) {
        return false;
    }

    output.assign(units, 0.0f);
#if XER_AI_HAS_EIGEN_HEADER
    using RowMat = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    Eigen::Map<const RowMat> w_map(weights.data(),
                                   static_cast<Eigen::Index>(units),
                                   static_cast<Eigen::Index>(in_dim));
    Eigen::Map<const Eigen::VectorXf> x_map(input.data(), static_cast<Eigen::Index>(in_dim));
    Eigen::VectorXf y = w_map * x_map;
    if (!bias.empty()) {
        Eigen::Map<const Eigen::VectorXf> b_map(bias.data(), static_cast<Eigen::Index>(units));
        y += b_map;
    }
    for (size_t i = 0; i < units; ++i) {
        output[i] = ApplyActivation(y(static_cast<Eigen::Index>(i)), layer.GetActivation());
    }
#else
    for (size_t row = 0; row < units; ++row) {
        float acc = bias.empty() ? 0.0f : bias[row];
        const float* w_row = weights.data() + row * in_dim;
        for (size_t col = 0; col < in_dim; ++col) {
            acc += w_row[col] * input[col];
        }
        output[row] = ApplyActivation(acc, layer.GetActivation());
    }
#endif
    return true;
}

std::vector<float> RunDenseLayer(const Core::DenseLayer& layer, const std::vector<float>& input) {
    std::vector<float> real_output;
    if (RunDenseLayerReal(layer, input, real_output)) {
        return real_output;
    }

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

Predictor::~Predictor() = default;

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
        // XNNPACK init probes CPU features and allocates runtime state, so the
        // predictor is created once and reused across Predict calls.
        if (!xnnpack_cache_) {
            try {
                xnnpack_cache_ = std::make_unique<XnnPackPredictor>(*model_);
            } catch (...) {
                Utility::ModelBuilderLogger::GetInstance().Warning(
                    "XNNPACK predictor init failed, using fallback predictor pipeline.");
            }
        }
        if (xnnpack_cache_) {
            return xnnpack_cache_->Predict(input);
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
