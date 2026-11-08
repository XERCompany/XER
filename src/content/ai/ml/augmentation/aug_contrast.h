#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugContrast : public AugBase {
public:
    explicit AugContrast(float factor = 1.5f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Contrast"; }
    void SetFactor(float f) { factor_ = f; }
private:
    float factor_ = 1.5f;
};

}  // namespace XER::ML::Augmentation
