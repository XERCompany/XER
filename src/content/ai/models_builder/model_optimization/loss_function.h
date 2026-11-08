#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace XER::ModelsBuilder::Optimization {

enum class LossType : uint8_t {
    MeanSquaredError = 0,
    CrossEntropy = 1,
    BinaryCrossEntropy = 2,
    KLDivergence = 3
};

class LossFunction {
public:
    virtual ~LossFunction() = default;
    
    virtual float Compute(const float* predictions, const float* targets, size_t size) = 0;
    virtual std::string GetLossName() const = 0;
};

class MSELoss : public LossFunction {
public:
    float Compute(const float* predictions, const float* targets, size_t size) override;
    std::string GetLossName() const override { return "MSE"; }
};

} // namespace XER::ModelsBuilder::Optimization
