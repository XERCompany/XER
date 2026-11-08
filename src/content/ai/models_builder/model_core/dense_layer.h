#pragma once

#include "layer.h"

namespace XER::ModelsBuilder::Core {

class DenseLayer : public Layer {
public:
    explicit DenseLayer(uint32_t units);
    
    uint32_t GetUnits() const { return units_; }
    void SetActivation(ActivationType act) { activation_ = act; }
    ActivationType GetActivation() const { return activation_; }
    
    bool ValidateInputShape(const std::vector<uint32_t>& input_shape) const override;
    std::vector<uint32_t> ComputeOutputShape(const std::vector<uint32_t>& input_shape) override;
    bool Initialize() override;
    std::string Describe() const override;

private:
    uint32_t units_;
    ActivationType activation_;
    uint32_t input_units_;
    bool initialized_;
};

} // namespace XER::ModelsBuilder::Core
