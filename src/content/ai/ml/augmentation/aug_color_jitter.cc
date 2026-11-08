#include "aug_color_jitter.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace XER::ML::Augmentation {

AugColorJitter::AugColorJitter(float b, float c, float s, float h)
    : brightness_(b), contrast_(c), saturation_(s), hue_(h) {}

bool AugColorJitter::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty() || sample.channels < 3) return false;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> bd(-brightness_, brightness_);
    std::uniform_real_distribution<float> cd(1.0f - contrast_, 1.0f + contrast_);
    std::uniform_real_distribution<float> hd(-hue_, hue_);

    const float b_off  = bd(rng);
    const float c_fac  = cd(rng);
    const float h_off  = hd(rng);  // hue shift in [0,1] space

    const int W = sample.width;
    const int H = sample.height;
    auto& px = sample.pixels;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float r = px[0 * H * W + y * W + x];
            float g = px[1 * H * W + y * W + x];
            float b = px[2 * H * W + y * W + x];

            // Brightness
            r += b_off; g += b_off; b += b_off;
            // Contrast around mean
            float mean = (r + g + b) / 3.0f;
            r = mean + c_fac * (r - mean);
            g = mean + c_fac * (g - mean);
            b = mean + c_fac * (b - mean);
            // Simple hue cycle (rotate R→G→B channels by h_off fraction)
            float hr = std::clamp(r + h_off, 0.0f, 1.0f);
            float hg = std::clamp(g,          0.0f, 1.0f);
            float hb = std::clamp(b - h_off,  0.0f, 1.0f);

            px[0 * H * W + y * W + x] = hr;
            px[1 * H * W + y * W + x] = hg;
            px[2 * H * W + y * W + x] = hb;
        }
    }
    return true;
}

}  // namespace XER::ML::Augmentation
