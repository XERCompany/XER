#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Scale features to [min_val, max_val] (default [0,1])
class PreprocessMinMax : public PreprocessBase {
public:
    explicit PreprocessMinMax(float min_val = 0.0f, float max_val = 1.0f)
        : range_min_(min_val), range_max_(max_val) {}

    void Fit(const std::vector<std::vector<float>>& X) override;
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "MinMaxScaler"; }

private:
    float range_min_, range_max_;
    std::vector<float> data_min_, data_max_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
