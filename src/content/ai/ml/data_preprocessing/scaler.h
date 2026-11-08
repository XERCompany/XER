#pragma once

#include "preprocessor.h"

namespace XER::MLData::Preprocessing {

class Scaler : public Preprocessor {
public:
    enum class ScalingMode : uint8_t {
        MinMax = 0,
        StandardScore = 1,
        RobustScale = 2
    };
    
    explicit Scaler(ScalingMode mode = ScalingMode::MinMax);
    
    std::shared_ptr<Types::Tensor> Process(const Types::Tensor& input) override;
    std::string GetProcessorName() const override { return "Scaler"; }

private:
    ScalingMode mode_;
};

} // namespace XER::MLData::Preprocessing
