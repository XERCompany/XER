#include "content/ai/sd_base/tensor/sd_latent_tensor.h"

#include <algorithm>
#include <cmath>

namespace XER::AI::SDBase {

XER::MLData::Types::Tensor SdLatentTensor::Make(uint32_t width, uint32_t height) {
    const uint32_t lw = std::max<uint32_t>(1, width / 8);
    const uint32_t lh = std::max<uint32_t>(1, height / 8);
    return XER::MLData::Types::Tensor({1, 4, lh, lw}, XER::MLData::Types::DataType::FLOAT32);
}

void SdLatentTensor::FillGaussian(XER::MLData::Types::Tensor& latent,
                                  XER::ML::Utils::MlRandomGenerator& rng,
                                  float sigma) {
    if (latent.GetDataType() != XER::MLData::Types::DataType::FLOAT32 || !latent.GetData()) {
        return;
    }

    float* data = static_cast<float*>(latent.GetData());
    const uint64_t n = latent.GetElementCount();
    for (uint64_t i = 0; i < n; i += 2) {
        const float u1 = std::max(1e-7f, rng.UniformFloat(0.0f, 1.0f));
        const float u2 = rng.UniformFloat(0.0f, 1.0f);
        const float r = std::sqrt(-2.0f * std::log(u1));
        const float th = 6.283185307179586f * u2;
        const float z0 = r * std::cos(th) * sigma;
        const float z1 = r * std::sin(th) * sigma;
        data[i] = z0;
        if (i + 1 < n) data[i + 1] = z1;
    }
}

}  // namespace XER::AI::SDBase
