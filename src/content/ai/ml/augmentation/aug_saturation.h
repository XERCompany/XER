#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugSaturation : public AugBase {
public:
    explicit AugSaturation(float factor = 1.5f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Saturation"; }
private:
    float factor_;
};

}  // namespace XER::ML::Augmentation
