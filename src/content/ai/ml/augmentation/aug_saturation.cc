#include "aug_saturation.h"
#include <algorithm>

namespace XER::ML::Augmentation {

AugSaturation::AugSaturation(float factor) : factor_(factor) {}

bool AugSaturation::Apply(ImageSample& sample) {
    if (!enabled_ || sample.channels < 3 || sample.pixels.empty()) return false;
    const int W = sample.width, H = sample.height;
    auto& px = sample.pixels;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float r = px[0 * H * W + y * W + x];
            float g = px[1 * H * W + y * W + x];
            float b = px[2 * H * W + y * W + x];
            float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            px[0 * H * W + y * W + x] = std::clamp(luma + factor_ * (r - luma), 0.0f, 1.0f);
            px[1 * H * W + y * W + x] = std::clamp(luma + factor_ * (g - luma), 0.0f, 1.0f);
            px[2 * H * W + y * W + x] = std::clamp(luma + factor_ * (b - luma), 0.0f, 1.0f);
        }
    }
    return true;
}

}  // namespace XER::ML::Augmentation
