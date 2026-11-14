#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugMixup : public AugBase {
public:
    explicit AugMixup(float alpha = 0.4f);
    // Blend two samples: result stored in a, b is consumed.
    bool Blend(ImageSample& a, const ImageSample& b, float lambda = -1.0f);
    bool Apply(ImageSample&) override { return false; }  // needs two samples
    std::string Name() const override { return "Mixup"; }
private:
    float alpha_;
    float SampleLambda() const;
};

}  // namespace XER::ML::Augmentation
