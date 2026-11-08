#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugRotation : public AugBase {
public:
    explicit AugRotation(float max_angle_deg = 15.0f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Rotation"; }
private:
    float max_angle_deg_;
};

}  // namespace XER::ML::Augmentation
