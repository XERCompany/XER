#pragma once

#include "preprocessor.h"

namespace XER::MLData::Preprocessing {

class Normalizer : public Preprocessor {
public:
    Normalizer();
    
    std::shared_ptr<Types::Tensor> Process(const Types::Tensor& input) override;
    std::string GetProcessorName() const override { return "Normalizer"; }
    
    void SetMean(float mean) { mean_ = mean; }
    void SetStdDev(float stddev) { stddev_ = stddev; }

private:
    float mean_;
    float stddev_;
};

} // namespace XER::MLData::Preprocessing
