#include "aug_cutout.h"
#include <algorithm>
#include <limits>
#include <random>

namespace Engine::ML::Augmentation {

AugCutout::AugCutout(int pw, int ph, float fill) : patch_w_(pw), patch_h_(ph), fill_(fill) {}

bool AugCutout::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty() || sample.width <= 0 || sample.height <= 0 ||
        sample.channels <= 0 || patch_w_ <= 0 || patch_h_ <= 0) {
        return false;
    }

    const size_t width = static_cast<size_t>(sample.width);
    const size_t height = static_cast<size_t>(sample.height);
    const size_t channels = static_cast<size_t>(sample.channels);
    if (width > std::numeric_limits<size_t>::max() / height) return false;
    const size_t plane_size = width * height;
    if (plane_size > std::numeric_limits<size_t>::max() / channels ||
        sample.pixels.size() != plane_size * channels) {
        return false;
    }

    const int patch_width = std::min(patch_w_, sample.width);
    const int patch_height = std::min(patch_h_, sample.height);
    thread_local std::mt19937 rng{std::random_device{}()};
    const int center_x = std::uniform_int_distribution<int>(0, sample.width - 1)(rng);
    const int center_y = std::uniform_int_distribution<int>(0, sample.height - 1)(rng);
    const int x0 = std::clamp(center_x - patch_width / 2, 0, sample.width - patch_width);
    const int y0 = std::clamp(center_y - patch_height / 2, 0, sample.height - patch_height);
    const int x1 = x0 + patch_width;
    const int y1 = y0 + patch_height;

    for (int channel = 0; channel < sample.channels; ++channel) {
        const size_t channel_offset = static_cast<size_t>(channel) * plane_size;
        for (int row = y0; row < y1; ++row) {
            for (int column = x0; column < x1; ++column) {
                const size_t pixel_index = channel_offset + static_cast<size_t>(row) * width +
                    static_cast<size_t>(column);
                sample.pixels[pixel_index] = fill_;
            }
        }
    }
    return true;
}

}  // namespace Engine::ML::Augmentation
