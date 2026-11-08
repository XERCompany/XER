#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

// Adds Gaussian noise to pixel values.
// Optionally uses spectral noise shaping via AudioFFTAnalyzer when available.
class AugGaussianNoise : public AugBase {
public:
    explicit AugGaussianNoise(float std_dev = 0.05f, bool spectral_shape = false);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "GaussianNoise"; }
    void SetStdDev(float s) { std_dev_ = s; }
private:
    float std_dev_;
    bool spectral_shape_;
};

}  // namespace XER::ML::Augmentation
