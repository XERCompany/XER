#include "content/ai/sd_base/controlnet/sd_controlnet_router.h"

#include <algorithm>

namespace XER::AI::SDBase {

void SdControlNetRouter::ApplyHintScale(XER::MLData::Types::Tensor& latent,
                                        float hint_strength) const {
    if (latent.GetDataType() != XER::MLData::Types::DataType::FLOAT32 || !latent.GetData()) {
        return;
    }

    const float scale = std::clamp(1.0f + hint_strength, 0.5f, 2.0f);
    float* d = static_cast<float*>(latent.GetData());
    const uint64_t n = latent.GetElementCount();
    for (uint64_t i = 0; i < n; ++i) {
        d[i] *= scale;
    }
}

}  // namespace XER::AI::SDBase
