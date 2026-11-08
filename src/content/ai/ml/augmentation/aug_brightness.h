#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugBrightness : public AugBase {
public:
    explicit AugBrightness(float delta = 0.2f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Brightness"; }
    void SetDelta(float d) { delta_ = d; }
private:
    float delta_ = 0.2f;
};

}  // namespace XER::ML::Augmentation
