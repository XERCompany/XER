#pragma once

#include "layer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Core {

/// @brief Model architecture type enumeration
/// 
/// Determines how layers are connected and how forward/backward passes execute
enum class ModelType : uint8_t {
    Sequential = 0,  ///< Linear stack: input → L1 → L2 → ... → output
    Functional = 1,  ///< Directed acyclic graph (DAG): multiple inputs/outputs
    Subclass = 2     ///< Custom class definition (user-defined forward pass)
};

/// @brief Neural network model container
/// 
/// Core model class that manages layers, topology, and computation flow.
/// Supports sequential and functional architecture patterns.
///
/// **Lifecycle:**
/// 1. Create: `Model m("name")`
/// 2. Add layers: `m.AddLayer(layer)`
/// 3. Build: `m.Build({batch_size, features})`
/// 4. Compile: `m.Compile()`
/// 5. Use for training/inference
///
/// **Example:**
/// ```cpp
/// auto model = std::make_unique<Model>("CNN");
/// model->SetModelType(ModelType::Sequential);
/// model->AddLayer(std::make_shared<Conv2DLayer>(32, 3));
/// model->AddLayer(std::make_shared<MaxPoolLayer>());
/// model->AddLayer(std::make_shared<DenseLayer>(128, 64));
/// model->Build({1, 28, 28});
/// if (!model->Compile()) { /*error*/ }
/// ```
class Model {
public:
    /// Create model with optional name
    /// @param name Model identifier for logging/serialization
    explicit Model(const std::string& name = "Model");
    
    /// Virtual destructor for inheritance
    virtual ~Model() = default;
    
    /// **Accessors**
    
    /// Get model name
    const std::string& GetModelName() const { return model_name_; }
    
    /// Get model architecture type
    ModelType GetModelType() const { return model_type_; }
    
    /// Check if model has been compiled
    /// @return true if Compile() succeeded
    bool IsCompiled() const { return is_compiled_; }
    
    /// Get input tensor shape (set during Build)
    /// @return Vector of dimensions [batch_size, ...feature_dims]
    const std::vector<uint32_t>& GetInputShape() const { return input_shape_; }
    
    /// Get output tensor shape (computed during Build)
    /// @return Vector of dimensions [batch_size, ...output_dims]
    const std::vector<uint32_t>& GetOutputShape() const { return output_shape_; }
    
    /// Get immutable reference to layer list
    const std::vector<std::shared_ptr<Layer>>& GetLayers() const { return layers_; }
    
    /// Get number of layers
    size_t GetLayerCount() const { return layers_.size(); }
    
    /// **Mutators**
    
    /// Set or rename model
    void SetModelName(const std::string& name) { model_name_ = name; }
    
    /// Set model architecture type
    void SetModelType(ModelType type) { model_type_ = type; }
    
    /// **Model Construction**
    
    /// Add layer to model
    /// @param layer Shared pointer to layer (ownership transferred)
    /// @throws ModelBuilderErrorCode::InvalidArgument if layer is null
    virtual void AddLayer(std::shared_ptr<Layer> layer);
    
    /// Remove all layers (reset model)
    /// @note Resets compiled state to false
    virtual void ClearLayers();
    
    /// **Build & Compilation Pipeline**
    
    /// Build model topology - validate shapes, initialize layers
    /// @param input_shape Input tensor shape [batch_size, ...features]
    /// @return true on success
    /// @note Must call before Compile()
    /// @note Sets output_shape_ by propagating input_shape_ through all layers
    virtual bool Build(const std::vector<uint32_t>& input_shape);
    
    /// Compile model - freeze topology and prepare for training/inference
    /// @return true on success
    /// @note Must call Build() first
    /// @throws ModelBuilderErrorCode::CompilationFailed if Build not called
    virtual bool Compile();
    
    /// **Layer Access**
    
    /// Get layer at index
    /// @param index Layer position in stack
    /// @return Shared pointer to layer, nullptr if out of bounds
    std::shared_ptr<Layer> GetLayer(size_t index) const;

protected:
    std::string model_name_;                      ///< Model identifier
    ModelType model_type_;                        ///< Sequential/Functional/Subclass
    std::vector<std::shared_ptr<Layer>> layers_;  ///< Layer stack
    bool is_compiled_;                            ///< Compilation state
    std::vector<uint32_t> input_shape_;           ///< Input shape from Build()
    std::vector<uint32_t> output_shape_;          ///< Output shape computed in Build()
};

} // namespace XER::ModelsBuilder::Core
