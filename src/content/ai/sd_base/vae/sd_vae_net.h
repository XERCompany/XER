#pragma once

#include "../weights/sd_safetensors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::AI::SDBase {

// Real VAE decoder forward pass (diffusers AutoencoderKL decoder layout).
// Self-contained: the decoder's own down_blocks produce the skip features
// needed by up_blocks, so a latent can be decoded without the encoder.
class SdVaeNet {
public:
    bool Load(const std::string& path, std::string* error);
    bool IsLoaded() const { return weights_.IsLoaded(); }

    // latent: [z_channels, h, w] CHW float (already un-scaled by caller or
    //         scaled inside via kVaeScale).
    // Returns [3, h*8, w*8] CHW float in ~[-1, 1] range (pre-clamp).
    // Empty vector on failure.
    std::vector<float> Decode(const float* latent, int h, int w) const;

private:
    SdWeightSet weights_;
};

}  // namespace XER::AI::SDBase
