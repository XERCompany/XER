#pragma once

#include "sd_base_types.h"

#include <string>

namespace XER::AI::SDBase {

struct SdBaseConfig {
    uint32_t max_width = 1024;
    uint32_t max_height = 1024;
    uint32_t min_steps = 1;
    uint32_t max_steps = 120;
    uint32_t default_steps = 30;
    uint32_t text_embedding_dim = 768;
    float init_noise_sigma = 1.0f;
    float eta = 0.0f;
    bool enable_cpu_fallback = true;
    bool prefer_cuda = true;

    // Backend preferences in descending priority.
    bool prefer_cudnn = true;
    bool prefer_cutlass = true;
    bool prefer_openvino = true;
    bool prefer_onnx = true;
    bool prefer_xnnpack = true;
    bool prefer_eigen = true;
    bool prefer_tensorflow = false;

    // Pipeline feature toggles.
    bool enable_sdxl = true;
    bool enable_controlnet = true;
    bool enable_vae = true;

    // Model weight locations.
    std::string model_root = "models/sd_base";
    std::string text_encoder_path = "models/sd_base/text_encoder.onnx";
    std::string unet_path = "models/sd_base/unet.onnx";
    std::string vae_decoder_path = "models/sd_base/vae_decoder.onnx";
    std::string controlnet_path = "models/sd_base/controlnet.onnx";
    std::string sdxl_text_encoder_2_path = "models/sd_base/sdxl_text_encoder_2.onnx";
    std::string sdxl_refiner_unet_path = "models/sd_base/sdxl_refiner_unet.onnx";
    bool strict_model_loading = false;
    bool allow_stub_inference = true;

    bool Validate(std::string* error) const;

    static SdBaseConfig FromPreset(SdQualityPreset preset);
};

}  // namespace XER::AI::SDBase
