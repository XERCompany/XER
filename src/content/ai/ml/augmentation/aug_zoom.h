#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugZoom : public AugBase {
public:
    AugZoom(float min_factor = 0.8f, float max_factor = 1.2f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Zoom"; }
private:
    float min_factor_, max_factor_;
};

}  // namespace XER::ML::Augmentation
