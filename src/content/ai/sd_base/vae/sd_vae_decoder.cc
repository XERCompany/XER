#include "content/ai/sd_base/vae/sd_vae_decoder.h"

#include "content/ai/sd_base/to/sd_image_decoder.h"

#include <algorithm>
#include <cmath>

namespace XER::AI::SDBase {

void SdVaeDecoder::SetWeightsPath(const std::string& path) {
    vae_net_ = SdVaeNet{};
    if (path.empty()) return;
    std::string err;
    vae_net_.Load(path, &err);
}

XER::MLData::Types::Tensor SdVaeDecoder::Decode(
    const XER::MLData::Types::Tensor& latent,
    uint32_t width,
    uint32_t height,
    bool high_quality) const {
    // Real VAE decode when weights are available.
    if (vae_net_.IsLoaded() && high_quality) {
        const auto lshape = latent.GetShape();
        if (lshape.size() == 4 && latent.GetData()) {
            const uint32_t lh = lshape[2];
            const uint32_t lw = lshape[3];
            const int zc = static_cast<int>(lshape[1]);
            const float* src = static_cast<const float*>(latent.GetData());
            // latent tensor layout: [1, C, H, W] -> CHW pointer at offset 0
            auto rgb = vae_net_.Decode(src, static_cast<int>(lh),
                                       static_cast<int>(lw));
            if (rgb.size() == static_cast<size_t>(3) * lh * 8 * lw * 8) {
                XER::MLData::Types::Tensor image(
                    {1, 3, lh * 8, lw * 8},
                    XER::MLData::Types::DataType::FLOAT32);
                float* dst = static_cast<float*>(image.GetData());
                const size_t plane = static_cast<size_t>(lh) * 8 * lw * 8;
                for (int ch = 0; ch < 3; ++ch) {
                    for (size_t i = 0; i < plane; ++i) {
                        dst[static_cast<size_t>(ch) * plane + i] =
                            std::clamp(0.5f + 0.5f * rgb[static_cast<size_t>(ch) * plane + i],
                                       0.0f, 1.0f);
                    }
                }
                (void)zc;
                return image;
            }
        }
    }

    SdImageDecoder decoder;
    auto image = decoder.DecodeLatentToImage(latent, width, height);
    if (!high_quality || !image.GetData() || image.GetDataType() != XER::MLData::Types::DataType::FLOAT32) {
        return image;
    }

    // Lightweight post-vae refinement pass to reduce banding on simple decoder output.
    float* data = static_cast<float*>(image.GetData());
    const uint64_t n = image.GetElementCount();
    for (uint64_t i = 0; i < n; ++i) {
        data[i] = std::clamp(0.5f + (data[i] - 0.5f) * 1.08f, 0.0f, 1.0f);
    }
    return image;
}

}  // namespace XER::AI::SDBase
