#include "aug_zoom.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace XER::ML::Augmentation {

AugZoom::AugZoom(float min_f, float max_f) : min_factor_(min_f), max_factor_(max_f) {}

bool AugZoom::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    const int W = sample.width, H = sample.height, C = sample.channels;
    thread_local std::mt19937 rng{std::random_device{}()};
    float fac = std::uniform_real_distribution<float>(min_factor_, max_factor_)(rng);

    const float cx = W / 2.0f, cy = H / 2.0f;
    std::vector<float> out(sample.pixels.size(), 0.0f);

    for (int c = 0; c < C; ++c) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sx = (x - cx) / fac + cx;
                float sy = (y - cy) / fac + cy;
                int ix = static_cast<int>(std::round(sx));
                int iy = static_cast<int>(std::round(sy));
                if (ix >= 0 && ix < W && iy >= 0 && iy < H)
                    out[c * H * W + y * W + x] = sample.pixels[c * H * W + iy * W + ix];
            }
        }
    }
    sample.pixels = std::move(out);
    return true;
}

}  // namespace XER::ML::Augmentation
