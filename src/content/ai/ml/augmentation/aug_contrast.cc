#include "aug_contrast.h"
#include <algorithm>
#include <numeric>

namespace XER::ML::Augmentation {

AugContrast::AugContrast(float factor) : factor_(factor) {}

bool AugContrast::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    float mean = 0.0f;
    for (auto v : sample.pixels) mean += v;
    mean /= static_cast<float>(sample.pixels.size());
    for (auto& p : sample.pixels)
        p = std::clamp(mean + factor_ * (p - mean), 0.0f, 1.0f);
    return true;
}

}  // namespace XER::ML::Augmentation
