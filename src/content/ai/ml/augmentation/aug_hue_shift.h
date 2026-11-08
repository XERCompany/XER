#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugHueShift : public AugBase {
public:
    explicit AugHueShift(float max_shift = 0.1f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "HueShift"; }
private:
    float max_shift_;
    static void RgbToHsv(float r, float g, float b, float& h, float& s, float& v);
    static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b);
};

}  // namespace XER::ML::Augmentation
