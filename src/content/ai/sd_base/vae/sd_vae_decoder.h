#pragma once

#include "content/ai/ml/data_types/tensor.h"
#include "content/ai/sd_base/vae/sd_vae_net.h"

#include <string>

namespace XER::AI::SDBase {

class SdVaeDecoder {
public:
    // Optional real VAE weights (.safetensors). When set and loaded,
    // Decode() runs the actual decoder network instead of the
    // latent->RGB approximation.
    void SetWeightsPath(const std::string& path);
    bool HasRealWeights() const { return vae_net_.IsLoaded(); }

    XER::MLData::Types::Tensor Decode(const XER::MLData::Types::Tensor& latent,
                                         uint32_t width,
                                         uint32_t height,
                                         bool high_quality) const;

private:
    SdVaeNet vae_net_;
};

}  // namespace XER::AI::SDBase
