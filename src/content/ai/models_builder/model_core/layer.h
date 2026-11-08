#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <string_view>

namespace XER::ModelsBuilder::Core {

/// @brief Layer type enumeration
/// 
/// Supported layer implementations in the neural network framework
enum class LayerType : uint8_t {
    Dense = 0,        ///< Fully connected layer (matmul + bias + activation)
    Conv2D = 1,       ///< 2D convolution (CuDNN-accelerated)
    MaxPool = 2,      ///< Max pooling
    Activation = 3,   ///< Standalone activation (ReLU, Sigmoid, Tanh, etc.)
    Dropout = 4,      ///< Stochastic regularization (random zeroing)
    BatchNorm = 5,    ///< Batch normalization with running stats
    Flatten = 6,      ///< Reshape multi-dimensional input to 2D
    LSTM = 7          ///< Long Short-Term Memory recurrent layer
};

/// @brief Activation function types
/// 
/// Element-wise nonlinearities applied after linear transformations
enum class ActivationType : uint8_t {
    ReLU = 0,         ///< Rectified Linear Unit: max(x, 0)
    Sigmoid = 1,      ///< Logistic: 1 / (1 + exp(-x))
    Tanh = 2,         ///< Hyperbolic tangent
    Linear = 3,       ///< Identity (no activation)
    SoftMax = 4       ///< Normalized exponential (for classification output)
};

/// @brief Base class for all layer types
/// 
/// Abstract interface for neural network layers. All layers implement:
/// - Forward pass computation
/// - Backward pass (gradient computation) 
/// - Shape validation and inference
/// - Parameter initialization
///
/// **Derivative Implementation:**
/// ```cpp
/// class MyCustomLayer : public Layer {
/// public:
///     MyCustomLayer() : Layer(LayerType::Custom) {}
///     
///     bool ValidateInputShape(const auto& shape) const override {
///         return shape.size() >= 2;  // At least batch and feature dims
///     }
///     
///     std::vector<uint32_t> ComputeOutputShape(const auto& input) override {
///         return {input[0], 128};  // batch_size x 128
///     }
///     
///     bool Initialize() override {
///         // Allocate weights, biases, allocate GPU memory
///         return true;
///     }
/// };
/// ```
class Layer {
public:
    /// Construct layer with given type
    /// @param type Layer type (Dense, Conv2D, etc.)
    explicit Layer(LayerType type);
    
    /// Virtual destructor for proper cleanup
    virtual ~Layer() = default;
    
    /// **Accessors**
    
    /// Get layer type
    LayerType GetLayerType() const { return layer_type_; }
    
    /// Get layer name (for debugging, logging)
    const std::string& GetLayerName() const { return layer_name_; }
    
    /// Get output shape (valid after Initialize)
    const std::vector<uint32_t>& GetOutputShape() const { return output_shape_; }
    
    /// **Mutators**
    
    /// Set layer name for logging
    /// @param name Debug identifier
    void SetLayerName(const std::string& name) { layer_name_ = name; }
    
    /// **Shape Validation**
    
    /// Validate if input shape is compatible with layer
    /// @param input_shape Input tensor shape [batch_size, ...features]
    /// @return true if shape is acceptable
    /// @note Called during Model::Build() to construct layer stack
    virtual bool ValidateInputShape(const std::vector<uint32_t>& input_shape) const;
    
    /// Compute output shape given input shape
    /// @param input_shape Input tensor shape [batch_size, ...features]
    /// @return Output shape [batch_size, ...output_features]
    /// @example Conv2D(28,28,32) + stride=1 → (28,28,32)
    /// @example Dense(1024,512) → (batch,512)
    /// @example Flatten(2,3,4) → (batch,24)
    virtual std::vector<uint32_t> ComputeOutputShape(const std::vector<uint32_t>& input_shape);
    
    /// **Initialization**
    
    /// Initialize layer internals (weights, GPU memory, CuDNN handles)
    /// @return true on success
    /// @note Called during Model::Build() after shape validation
    virtual bool Initialize() { return true; }
    
    /// **Debugging**
    
    /// Get human-readable layer description
    /// @return String: "DenseLayer(1024→512,ReLU)" or similar
    virtual std::string Describe() const;

protected:
    LayerType layer_type_;                        ///< Layer classification
    std::string layer_name_;                      ///< Debug identifier
    std::vector<uint32_t> output_shape_;          ///< Computed during Initialize
};

} // namespace XER::ModelsBuilder::Core
