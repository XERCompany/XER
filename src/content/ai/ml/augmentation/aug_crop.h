#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugCrop : public AugBase {
public:
    AugCrop(int crop_width, int crop_height, bool random = true);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Crop"; }
private:
    int crop_w_, crop_h_;
    bool random_;
};

}  // namespace XER::ML::Augmentation
