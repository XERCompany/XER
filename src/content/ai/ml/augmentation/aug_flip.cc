#include "aug_flip.h"
#include <algorithm>
#include <random>

namespace XER::ML::Augmentation {

AugFlip::AugFlip(Direction dir, float prob) : dir_(dir), prob_(prob) {}

bool AugFlip::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(rng) > prob_) return false;

    const int W = sample.width, H = sample.height, C = sample.channels;
    auto& px = sample.pixels;

    auto flip_h = [&]() {
        for (int c = 0; c < C; ++c)
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W / 2; ++x)
                    std::swap(px[c * H * W + y * W + x],
                              px[c * H * W + y * W + (W - 1 - x)]);
    };
    auto flip_v = [&]() {
        for (int c = 0; c < C; ++c)
            for (int y = 0; y < H / 2; ++y)
                for (int x = 0; x < W; ++x)
                    std::swap(px[c * H * W + y * W + x],
                              px[c * H * W + (H - 1 - y) * W + x]);
    };

    if (dir_ == Direction::Horizontal || dir_ == Direction::Both) flip_h();
    if (dir_ == Direction::Vertical   || dir_ == Direction::Both) flip_v();
    return true;
}

}  // namespace XER::ML::Augmentation
