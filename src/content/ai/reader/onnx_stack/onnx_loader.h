#pragma once

#include "../../models_builder/model_core/model.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>

namespace XER::ModelsBuilder::Reader::Onnx {

/// @brief ONNX model loader and parser
/// 
/// Loads ONNX (Open Neural Network Exchange) format models and converts them
/// to XER native model format with full shape inference and validation.
///
/// Supported ONNX operators:
/// - Linear: Gemm, MatMul, Conv2D, ConvTranspose2D
/// - Activation: ReLU, Sigmoid, Tanh, Softmax, LeakyReLU
/// - Normalization: BatchNormalization, LayerNormalization
/// - Pooling: MaxPool, AveragePool, GlobalAveragePool
/// - Reduction: ReduceSum, ReduceMean, ReduceMax, ReduceMin
/// - Advanced: LSTM, GRU, Attention, Reshape
class OnnxLoader {
public:
    /// Load ONNX model from file
    /// @param filepath Path to .onnx file
    /// @return Shared pointer to converted XER Model
    /// @throws std::runtime_error on parse error, invalid format, or unsupported ops
    static std::shared_ptr<Core::Model> Load(const std::string& filepath);
    
    /// Load ONNX model from binary buffer
    /// @param buffer Raw ONNX bytes
    /// @param size Buffer size in bytes
    /// @return Shared pointer to converted XER Model
    static std::shared_ptr<Core::Model> LoadFromBuffer(const uint8_t* buffer,
                                                         size_t size);
    
    /// Get ONNX model metadata (version, producer, etc.)
    /// @param filepath Path to .onnx file
    /// @return Metadata string (for logging/debugging)
    static std::string GetMetadata(const std::string& filepath);

private:
    OnnxLoader() = default;
};

} // namespace XER::ModelsBuilder::Reader::Onnx
