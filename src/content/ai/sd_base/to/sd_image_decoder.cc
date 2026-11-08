#include "content/ai/sd_base/to/sd_image_decoder.h"

#include <algorithm>

namespace XER::AI::SDBase {

XER::MLData::Types::Tensor SdImageDecoder::DecodeLatentToImage(
    const XER::MLData::Types::Tensor& latent,
    uint32_t width,
    uint32_t height) const {
    XER::MLData::Types::Tensor image({1, 3, height, width}, XER::MLData::Types::DataType::FLOAT32);
    if (!latent.GetData() || !image.GetData()) return image;

    const auto lshape = latent.GetShape();
    if (lshape.size() != 4) return image;

    const uint32_t lh = lshape[2];
    const uint32_t lw = lshape[3];
    const float* src = static_cast<const float*>(latent.GetData());
    float* dst = static_cast<float*>(image.GetData());

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sy = std::min<uint32_t>(lh - 1, (y * lh) / std::max<uint32_t>(1, height));
            const uint32_t sx = std::min<uint32_t>(lw - 1, (x * lw) / std::max<uint32_t>(1, width));
            const uint64_t base_lat = (0ULL * 4 + 0ULL) * lh * lw + sy * lw + sx;
            const float v = src[base_lat];
            const float rgb = std::clamp(0.5f + 0.25f * v, 0.0f, 1.0f);

            for (uint32_t c = 0; c < 3; ++c) {
                const uint64_t out_idx = (0ULL * 3 + c) * height * width + y * width + x;
                dst[out_idx] = rgb;
            }
        }
    }

    return image;
}

}  // namespace XER::AI::SDBase
