#include "aug_blur.h"
#include <algorithm>
#include <cmath>

namespace XER::ML::Augmentation {

AugBlur::AugBlur(int kernel_size) : kernel_size_(kernel_size | 1) {}

bool AugBlur::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty() || sample.width < 2 || sample.height < 2) return false;

    const int W = sample.width;
    const int H = sample.height;
    const int C = sample.channels;
    const int half = kernel_size_ / 2;
    std::vector<float> out(sample.pixels.size(), 0.0f);

    // Simple box blur per channel
    for (int c = 0; c < C; ++c) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sum = 0.0f;
                int count = 0;
                for (int ky = -half; ky <= half; ++ky) {
                    for (int kx = -half; kx <= half; ++kx) {
                        int ny = std::clamp(y + ky, 0, H - 1);
                        int nx = std::clamp(x + kx, 0, W - 1);
                        sum += sample.pixels[c * H * W + ny * W + nx];
                        ++count;
                    }
                }
                out[c * H * W + y * W + x] = sum / static_cast<float>(count);
            }
        }
    }
    sample.pixels = std::move(out);
    return true;
}

}  // namespace XER::ML::Augmentation
