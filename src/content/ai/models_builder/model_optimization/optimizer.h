#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace XER::ModelsBuilder::Optimization {

enum class OptimizerType : uint8_t {
    SGD = 0,
    Adam = 1,
    RMSprop = 2,
    AdaGrad = 3
};

class Optimizer {
public:
    virtual ~Optimizer() = default;
    
    virtual std::string GetOptimizerName() const = 0;
    virtual float GetLearningRate() const = 0;
    virtual void SetLearningRate(float lr) = 0;
};

class AdamOptimizer : public Optimizer {
public:
    explicit AdamOptimizer(float learning_rate = 0.001f);
    
    std::string GetOptimizerName() const override { return "Adam"; }
    float GetLearningRate() const override { return learning_rate_; }
    void SetLearningRate(float lr) override { learning_rate_ = lr; }
    
    float GetBeta1() const { return beta1_; }
    float GetBeta2() const { return beta2_; }

private:
    float learning_rate_;
    float beta1_;
    float beta2_;
};

} // namespace XER::ModelsBuilder::Optimization
