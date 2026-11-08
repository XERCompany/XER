#pragma once

#include "content/ai/ml/data_types/tensor.h"

#include <cstdint>
#include <map>
#include <string>

namespace XER::AI::SDBase {

enum class SdQualityPreset : uint8_t {
    Draft = 0,
    Balanced = 1,
    High = 2,
};

enum class SdSchedulerType : uint8_t {
    Euler = 0,
    DDIM = 1,
};

struct SdRuntimeCapabilities {
    bool cuda_available = false;
    bool cudnn_available = false;
    bool cutlass_available = false;
    bool openvino_available = false;
    bool onnx_available = false;
    bool tensorflow_available = false;
    bool opencv_available = false;
    bool ffmpeg_available = false;
    bool eigen_available = false;
    bool xnnpack_available = false;
    std::string selected_backend;
};

struct SdGenerationRequest {
    std::string prompt;
    std::string negative_prompt;
    uint32_t width = 512;
    uint32_t height = 512;
    uint32_t steps = 30;
    float guidance_scale = 7.5f;
    uint64_t seed = 0;
    SdQualityPreset preset = SdQualityPreset::Balanced;
    SdSchedulerType scheduler = SdSchedulerType::Euler;

    // Feature toggles
    bool enable_sdxl = true;
    bool enable_controlnet = true;
    bool enable_vae_decode = true;

    // Conditioning and routing
    float controlnet_strength = 0.05f;
    float depth_strength = 0.10f;
    std::string controlnet_hint;
    std::string backend_hint;

    // Optional per-request weight overrides.
    std::string text_encoder_path;
    std::string unet_path;
    std::string vae_decoder_path;
    std::string controlnet_path;
    std::string sdxl_text_encoder_2_path;
    std::string sdxl_refiner_unet_path;
    bool strict_model_loading = false;
};

struct SdGenerationResult {
    bool ok = false;
    std::string error;
    XER::MLData::Types::Tensor latent = XER::MLData::Types::Tensor({1, 4, 64, 64});
    XER::MLData::Types::Tensor image = XER::MLData::Types::Tensor({1, 3, 512, 512});
    std::map<std::string, std::string> metadata;
};

}  // namespace XER::AI::SDBase
