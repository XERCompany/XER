#pragma once
#include "preprocess_base.h"
#include <cstddef>

namespace XER::ML::Preprocessing {

/// Resample (upsample or downsample) feature sequences to target_length
class PreprocessResampler : public PreprocessBase {
public:
    explicit PreprocessResampler(size_t target_length)
        : target_length_(target_length) {}
    void Fit(const std::vector<std::vector<float>>&) override { fitted_ = true; }
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "Resampler"; }
private:
    size_t target_length_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
