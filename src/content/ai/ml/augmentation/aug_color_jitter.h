#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugColorJitter : public AugBase {
public:
    AugColorJitter(float brightness = 0.2f, float contrast = 0.2f,
                   float saturation = 0.2f, float hue = 0.05f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "ColorJitter"; }
private:
    float brightness_, contrast_, saturation_, hue_;
};

}  // namespace XER::ML::Augmentation
