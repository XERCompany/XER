#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Quantize features to `n_bins` uniformly spaced bins per feature
class PreprocessQuantizer : public PreprocessBase {
public:
    explicit PreprocessQuantizer(int n_bins = 10) : n_bins_(n_bins) {}
    void Fit(const std::vector<std::vector<float>>& X) override;
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "Quantizer"; }
private:
    int n_bins_;
    std::vector<float> data_min_, data_max_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
