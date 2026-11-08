#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Normalize each sample row to unit L2 norm
class PreprocessNormalizer : public PreprocessBase {
public:
    void Fit(const std::vector<std::vector<float>>&) override { fitted_ = true; }
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "Normalizer"; }
private:
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
