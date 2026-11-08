#pragma once
#include "preprocess_base.h"
#include <cstddef>

namespace XER::ML::Preprocessing {

/// Smooth feature sequences with a simple moving-average window
class PreprocessNoiseFilter : public PreprocessBase {
public:
    explicit PreprocessNoiseFilter(size_t window = 3) : window_(window) {}
    void Fit(const std::vector<std::vector<float>>&) override { fitted_ = true; }
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "NoiseFilter"; }
private:
    size_t window_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
