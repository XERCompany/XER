#include "predictor.h"

#include "../model_core/dense_layer.h"
#include "xnnpack_predictor.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
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
#include <ggml-alloc.h>
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

#if XER_PRED_HAS_GGML

namespace {

// Process-wide ggml backend: prefers a discrete/integrated GPU device
// (CUDA via ggml-cuda, Vulkan via ggml-vulkan, ...) and falls back to CPU.
// Backend init is expensive (CUDA context ~hundreds of ms), so it is
// created once and reused; graph computes are serialized because ggml
// backends are not safe for concurrent graph_compute on the same instance.
struct GgmlBackendLease {
    ggml_backend_dev_t dev = nullptr;
    ggml_backend_t backend = nullptr;
    bool is_cpu = false;
    std::mutex mutex;

    ~GgmlBackendLease() {
        if (backend != nullptr) {
            ggml_backend_free(backend);
        }
    }
};

GgmlBackendLease& GgmlBackend() {
    // Heap-allocated on purpose: lives for the whole process (ggml backend
    // teardown ordering vs static destruction is not worth the risk), and
    // std::mutex makes the lease non-movable so it cannot be a value static.
    static GgmlBackendLease* lease = [] {
        auto* l = new GgmlBackendLease();
        ggml_backend_load_all();  // registers built-ins + dlopens ggml-*.{dll,so}

        static constexpr enum ggml_backend_dev_type kPreferred[] = {
            GGML_BACKEND_DEVICE_TYPE_GPU,
            GGML_BACKEND_DEVICE_TYPE_IGPU,
            GGML_BACKEND_DEVICE_TYPE_ACCEL,
            GGML_BACKEND_DEVICE_TYPE_CPU,
        };
        for (const enum ggml_backend_dev_type type : kPreferred) {
            ggml_backend_dev_t candidate = ggml_backend_dev_by_type(type);
            if (candidate == nullptr) {
                continue;
            }
            ggml_backend_t b = ggml_backend_dev_init(candidate, nullptr);
            if (b != nullptr) {
                l->dev = candidate;
                l->backend = b;
                l->is_cpu = (type == GGML_BACKEND_DEVICE_TYPE_CPU);
                break;
            }
        }
        if (l->backend == nullptr && ggml_backend_dev_count() > 0) {
            // Registry non-empty but preferred types absent — take dev 0.
            l->dev = ggml_backend_dev_get(0);
            l->backend = ggml_backend_dev_init(l->dev, nullptr);
        }
        if (l->backend == nullptr) {
            // Last resort: the CPU backend is always linked into ggml::ggml.
            l->backend = ggml_backend_cpu_init();
            l->is_cpu = true;
        }
        // One-time diagnostic: which device actually serves ggml graphs.
        static const bool logged = [l] {
            const char* name = (l->dev != nullptr && ggml_backend_dev_name(l->dev) != nullptr)
                                   ? ggml_backend_dev_name(l->dev)
                                   : (l->is_cpu ? "CPU" : "unknown");
            Utility::ModelBuilderLogger::GetInstance().Info(
                std::string("ggml backend selected: ") + name);
            return true;
        }();
        (void)logged;
        return l;
    }();
    return *lease;
}

} // namespace

#endif // XER_PRED_HAS_GGML

// ggml graph executor: y = act(W*x + b) on a real ggml compute graph.
// Row-major W[units x in] maps directly onto ggml ne={in, units} layout.
// Runs on the best available ggml backend (CUDA/Vulkan GPU preferred,
// CPU fallback) through the backend-agnostic ggml_backend_* API.
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

    GgmlBackendLease& lease = GgmlBackend();
    if (lease.backend == nullptr) {
        return false;
    }

    const size_t mem_size = weights.size() * sizeof(float) +
                            input.size() * sizeof(float) +
                            units * sizeof(float) * 4U + (4U << 20U);
    ggml_init_params params = {mem_size, nullptr, true /* no_alloc */};
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
    if (y == nullptr) {
        ggml_free(ctx);
        return false;
    }

    ggml_cgraph* graph = ggml_new_graph(ctx);
    if (graph == nullptr) {
        ggml_free(ctx);
        return false;
    }
    ggml_build_forward_expand(graph, y);

    // Allocate every ctx tensor on the selected backend's buffer.
    ggml_backend_buffer_t buffer = ggml_backend_alloc_ctx_tensors(ctx, lease.backend);
    if (buffer == nullptr) {
        ggml_free(ctx);
        return false;
    }

    const size_t w_bytes = weights.size() * sizeof(float);
    const size_t x_bytes = input.size() * sizeof(float);
    const size_t y_bytes = units * sizeof(float);
    ggml_backend_tensor_set(w, weights.data(), 0, w_bytes);
    ggml_backend_tensor_set(x, input.data(), 0, x_bytes);
    if (b != nullptr) {
        ggml_backend_tensor_set(b, bias.data(), 0, bias.size() * sizeof(float));
    }

    output.assign(units, 0.0f);
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(lease.mutex);
        if (lease.is_cpu) {
            ggml_backend_cpu_set_n_threads(lease.backend, static_cast<int>(
                std::min<size_t>(Utility::SuggestedInferenceThreadCount(), 64U)));
        }
        ok = ggml_backend_graph_compute(lease.backend, graph) == GGML_STATUS_SUCCESS;
        if (ok) {
            ggml_backend_tensor_get(y, output.data(), 0, y_bytes);
        }
    }

    ggml_backend_buffer_free(buffer);
    ggml_free(ctx);
    return ok;
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
