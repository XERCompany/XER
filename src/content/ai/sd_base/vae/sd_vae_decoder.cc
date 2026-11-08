#include "content/ai/sd_base/vae/sd_vae_decoder.h"

#include "content/ai/sd_base/to/sd_image_decoder.h"

#include <algorithm>

namespace XER::AI::SDBase {

XER::MLData::Types::Tensor SdVaeDecoder::Decode(const XER::MLData::Types::Tensor& latent,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    bool high_quality) const {
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
