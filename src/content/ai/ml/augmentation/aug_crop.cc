#include "aug_crop.h"
#include <algorithm>
#include <random>

namespace XER::ML::Augmentation {

AugCrop::AugCrop(int crop_width, int crop_height, bool random)
    : crop_w_(crop_width), crop_h_(crop_height), random_(random) {}

bool AugCrop::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    const int W = sample.width;
    const int H = sample.height;
    const int C = sample.channels;
    const int cw = std::min(crop_w_, W);
    const int ch = std::min(crop_h_, H);

    int x0 = 0, y0 = 0;
    if (random_) {
        thread_local std::mt19937 rng{std::random_device{}()};
        if (W > cw) x0 = std::uniform_int_distribution<int>(0, W - cw)(rng);
        if (H > ch) y0 = std::uniform_int_distribution<int>(0, H - ch)(rng);
    }

    std::vector<float> out(C * ch * cw);
    for (int c = 0; c < C; ++c)
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                out[c * ch * cw + y * cw + x] = sample.pixels[c * H * W + (y0 + y) * W + (x0 + x)];

    sample.pixels = std::move(out);
    sample.width  = cw;
    sample.height = ch;
    return true;
}

}  // namespace XER::ML::Augmentation
