#pragma once

#include "content/ai/sd_base/core/sd_base_config.h"
#include "content/ai/sd_base/core/sd_base_types.h"
#include "content/ai/sd_base/gpu/sd_gpu_dispatch.h"
#include "content/ai/sd_base/unet/sd_unet.h"

#include <string>
#include <vector>

namespace XER::AI::SDBase {

struct SdLoadedModelSet {
    bool text_encoder_loaded = false;
    bool unet_loaded = false;
    bool vae_loaded = false;
    bool controlnet_loaded = false;
    bool sdxl_branch_loaded = false;
};

class SdModelRuntime {
public:
    bool Load(const SdBaseConfig& config,
              const SdGenerationRequest& request,
              const SdRuntimeCapabilities& caps,
              SdBackendKind backend,
              std::string* error_out);

    std::vector<float> EncodeText(const std::string& prompt, uint32_t dim) const;

    void PredictNoise(const std::vector<float>& text_embedding,
                      float* latent,
                      uint64_t element_count,
                      uint32_t timestep,
                      float sigma,
                      float guidance) const;

    // Classifier-free guidance: unconditional (negative/empty) + conditional
    // UNet passes combined as eps = uncond + g * (cond - uncond).
    void PredictNoiseCfg(const std::vector<float>& cond_embedding,
                         const std::vector<float>& uncond_embedding,
                         float* latent,
                         uint64_t element_count,
                         uint32_t timestep,
                         float sigma,
                         float guidance) const;

    bool HasRealUnet() const { return unet_real_; }
    const SdLoadedModelSet& loaded() const { return loaded_; }

private:
    static bool Exists(const std::string& path);
    void PredictNoiseSynthetic(const std::vector<float>& text_embedding,
                               float* latent,
                               uint64_t element_count,
                               uint32_t timestep,
                               float sigma,
                               float guidance) const;

    SdBackendKind backend_ = SdBackendKind::CpuEigen;
    SdLoadedModelSet loaded_;
    SdUnet unet_;
    bool unet_real_ = false;
    int latent_h_ = 0;
    int latent_w_ = 0;
    int ctx_len_ = 77;
    int ctx_dim_ = 768;
};

}  // namespace XER::AI::SDBase
