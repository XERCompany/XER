#include "aug_rotation.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace XER::ML::Augmentation {

AugRotation::AugRotation(float max_angle_deg) : max_angle_deg_(max_angle_deg) {}

bool AugRotation::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    const int W = sample.width, H = sample.height, C = sample.channels;

    thread_local std::mt19937 rng{std::random_device{}()};
    float angle = std::uniform_real_distribution<float>(-max_angle_deg_, max_angle_deg_)(rng);
    const float rad = angle * 3.14159265f / 180.0f;
    const float cos_a = std::cos(rad), sin_a = std::sin(rad);
    const float cx = W / 2.0f, cy = H / 2.0f;

    std::vector<float> out(sample.pixels.size(), 0.0f);
    for (int c = 0; c < C; ++c) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float dx = x - cx, dy = y - cy;
                float sx = cos_a * dx + sin_a * dy + cx;
                float sy = -sin_a * dx + cos_a * dy + cy;
                int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
                if (ix >= 0 && ix < W && iy >= 0 && iy < H)
                    out[c * H * W + y * W + x] = sample.pixels[c * H * W + iy * W + ix];
            }
        }
    }
    sample.pixels = std::move(out);
    return true;
}

}  // namespace XER::ML::Augmentation
