#include "aug_brightness.h"
#include <algorithm>

namespace XER::ML::Augmentation {

AugBrightness::AugBrightness(float delta) : delta_(delta) {}

bool AugBrightness::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    for (auto& p : sample.pixels)
        p = std::clamp(p + delta_, 0.0f, 1.0f);
    return true;
}

}  // namespace XER::ML::Augmentation
