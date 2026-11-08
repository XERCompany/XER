#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Binarize features: value >= threshold → 1.0, else 0.0
class PreprocessBinarizer : public PreprocessBase {
public:
    explicit PreprocessBinarizer(float threshold = 0.0f) : threshold_(threshold) {}
    void Fit(const std::vector<std::vector<float>>&) override { fitted_ = true; }
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "Binarizer"; }
private:
    float threshold_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
