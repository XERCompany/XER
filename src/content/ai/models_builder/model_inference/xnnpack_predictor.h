#pragma once

#include "../model_core/model.h"
#include "../model_core/layer.h"
#include <memory>
#include <vector>

// Forward declare XNNPACK
typedef struct xnn_subgraph* xnn_subgraph_t;
typedef struct xnn_runtime* xnn_runtime_t;

namespace XER::ModelsBuilder::Inference {

/// @brief XNNPACK-accelerated model inference engine
/// 
/// High-performance inference using XNNPACK's optimized operator library.
/// Features:
/// - Automatic operator fusion (Conv+ReLU, MatMul+Add, etc.)
/// - Quantization support (int8, fp16 inference)
/// - Shallow-copy weight sharing
/// - Multi-threaded inference via pthreadpool
///
/// **Usage:**
/// ```cpp
/// auto predictor = std::make_unique<XnnPackPredictor>(model);
/// auto output = predictor->Predict(input);
/// ```
class XnnPackPredictor {
public:
    /// Create predictor for model
    /// @param model Compiled neural network model
    /// @throws std::runtime_error on initialization failure
    explicit XnnPackPredictor(const Core::Model& model);
    
    /// Destructor - cleans up XNNPACK resources
    ~XnnPackPredictor();
    
    /// Run inference on input batch
    /// @param input Input tensor (shape must match model input)
    /// @return Output tensor (shape matches model output)
    std::vector<float> Predict(const std::vector<float>& input);
    
    /// Run inference with pre-allocated output buffer
    /// @param input Input data
    /// @param output Output buffer (must be large enough)
    void Predict(const std::vector<float>& input, std::vector<float>& output);
    
    /// Get number of threads for multithreaded inference
    size_t GetNumThreads() const;
    
    /// Set number of threads (default: number of CPU cores)
    void SetNumThreads(size_t num_threads);
    
    /// Get estimated inference latency in milliseconds (for current batch size)
    float GetLatencyMs() const { return latency_ms_; }

private:
    const Core::Model& model_;
    xnn_subgraph_t subgraph_;
    xnn_runtime_t runtime_;
    float latency_ms_;
    size_t num_threads_;
    bool xnnpack_ready_;
    
    // Helper methods
    bool BuildSubgraph();
    bool CreateRuntime();
    bool ValidateInput(const std::vector<float>& input) const;
};

/// @brief Quantized model inference (int8)
/// 
/// Post-training quantization wrapper for reduced memory and latency
/// Converts fp32 weights to int8 with scale/zero-point per-channel
class QuantizedPredictor {
public:
    /// Create quantized predictor
    /// @param model Compiled model
    /// @param calibration_data Sample data for quantization calibration
    explicit QuantizedPredictor(const Core::Model& model,
                                const std::vector<std::vector<float>>& calibration_data);
    
    /// Run quantized inference
    /// @param input Input (fp32)
    /// @return Output (fp32)
    std::vector<float> Predict(const std::vector<float>& input);
    
    /// Get compression ratio (original size / quantized size)
    float GetCompressionRatio() const;

private:
    XnnPackPredictor xnnpack_pred_;
    std::vector<float> scales_;
    std::vector<uint8_t> zero_points_;
};

} // namespace XER::ModelsBuilder::Inference
