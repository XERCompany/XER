#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Remove outliers per feature using IQR rule; replaces with NaN-sentinel (0)
class PreprocessOutlierRemover : public PreprocessBase {
public:
    explicit PreprocessOutlierRemover(float iqr_factor = 1.5f)
        : iqr_factor_(iqr_factor) {}
    void Fit(const std::vector<std::vector<float>>& X) override;
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "OutlierRemover"; }
private:
    float iqr_factor_;
    std::vector<float> lower_, upper_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
