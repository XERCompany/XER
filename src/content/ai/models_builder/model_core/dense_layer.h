#pragma once

#include "layer.h"

#include <vector>

namespace XER::ModelsBuilder::Core {

class DenseLayer : public Layer {
public:
    explicit DenseLayer(uint32_t units);
    
    uint32_t GetUnits() const { return units_; }
    uint32_t GetInputUnits() const { return input_units_; }
    void SetActivation(ActivationType act) { activation_ = act; }
    ActivationType GetActivation() const { return activation_; }

    /// Attach learned parameters. Weights are row-major [units x input_units],
    /// bias has `units` entries. When present, inference performs a real
    /// forward pass y = act(W*x + b) instead of the synthetic fallback.
    void SetWeights(std::vector<float> weights, std::vector<float> bias = {});
    bool HasWeights() const { return !weights_.empty(); }
    const std::vector<float>& GetWeights() const { return weights_; }
    const std::vector<float>& GetBias() const { return bias_; }
    
    bool ValidateInputShape(const std::vector<uint32_t>& input_shape) const override;
    std::vector<uint32_t> ComputeOutputShape(const std::vector<uint32_t>& input_shape) override;
    bool Initialize() override;
    std::string Describe() const override;

private:
    uint32_t units_;
    ActivationType activation_;
    uint32_t input_units_;
    bool initialized_;
    std::vector<float> weights_;
    std::vector<float> bias_;
};

} // namespace XER::ModelsBuilder::Core
