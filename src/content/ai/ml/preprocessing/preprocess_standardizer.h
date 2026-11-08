#pragma once
#include "preprocess_base.h"
#include <vector>

namespace XER::ML::Preprocessing {

/// Standardize features: z = (x - mean) / std
class PreprocessStandardizer : public PreprocessBase {
public:
    void Fit(const std::vector<std::vector<float>>& X) override;
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "Standardizer"; }

    const std::vector<float>& Means() const { return mean_; }
    const std::vector<float>& StdDevs() const { return std_; }

private:
    std::vector<float> mean_, std_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
