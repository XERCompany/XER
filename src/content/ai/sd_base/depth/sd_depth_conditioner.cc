#include "content/ai/sd_base/depth/sd_depth_conditioner.h"

#include <algorithm>

namespace XER::AI::SDBase {

void SdDepthConditioner::ApplyDepthPrior(XER::MLData::Types::Tensor& latent,
                                         float depth_weight) const {
    if (latent.GetDataType() != XER::MLData::Types::DataType::FLOAT32 || !latent.GetData()) {
        return;
    }

    const float w = std::clamp(depth_weight, 0.0f, 1.0f);
    float* d = static_cast<float*>(latent.GetData());
    const uint64_t n = latent.GetElementCount();
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = d[i] * (1.0f - 0.1f * w);
    }
}

}  // namespace XER::AI::SDBase
