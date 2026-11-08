#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugCutout : public AugBase {
public:
    AugCutout(int patch_w = 16, int patch_h = 16, float fill = 0.0f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Cutout"; }
private:
    int patch_w_, patch_h_;
    float fill_;
};

}  // namespace XER::ML::Augmentation
