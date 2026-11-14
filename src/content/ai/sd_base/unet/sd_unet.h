#pragma once

#include "../weights/sd_safetensors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::AI::SDBase {

// Real UNet denoiser forward pass over safetensors weights.
// Supports the diffusers SD1.x/SD2.x UNet layout (down_blocks/up_blocks/
// mid_block with resnets, optional attention and down/up samplers).
// Weights are read once via SdWeightSet; all math is F32 on CPU.
class SdUnet {
public:
    // Load UNet weights from a .safetensors checkpoint.
    bool Load(const std::string& path, std::string* error);
    bool IsLoaded() const { return weights_.IsLoaded(); }
    const SdWeightSet& Weights() const { return weights_; }

    // x: noisy latent [4, h, w] CHW float.
    // ctx: cross-attention context tokens [ctx_len, ctx_dim] row-major
    //      (pass nullptr/0 to use a single pooled token replicated internally).
    // t: diffusion timestep (0..1000 scale).
    // out_eps: receives predicted noise [4, h, w].
    void Forward(const float* x, int h, int w,
                 const float* ctx, int ctx_len, int ctx_dim,
                 float t, float* out_eps) const;

private:
    SdWeightSet weights_;
};

}  // namespace XER::AI::SDBase
