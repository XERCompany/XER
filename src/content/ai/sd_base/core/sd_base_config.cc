#include "sd_base_config.h"

#include <filesystem>

namespace XER::AI::SDBase {

bool SdBaseConfig::Validate(std::string* error) const {
    if (max_width < 64 || max_height < 64) {
        if (error) *error = "max resolution is too small";
        return false;
    }
    if (min_steps == 0 || max_steps < min_steps) {
        if (error) *error = "invalid diffusion step range";
        return false;
    }
    if (default_steps < min_steps || default_steps > max_steps) {
        if (error) *error = "default steps are outside the configured range";
        return false;
    }
    if (text_embedding_dim == 0) {
        if (error) *error = "text embedding dimension must be > 0";
        return false;
    }
    if (init_noise_sigma <= 0.0f) {
        if (error) *error = "initial noise sigma must be > 0";
        return false;
    }
    if (!enable_cpu_fallback &&
        !prefer_cuda && !prefer_openvino && !prefer_onnx &&
        !prefer_xnnpack && !prefer_eigen && !prefer_tensorflow) {
        if (error) *error = "no SD backend path is enabled";
        return false;
    }
    if (strict_model_loading) {
        const auto exists = [](const std::string& p) {
            return !p.empty() && std::filesystem::exists(std::filesystem::path(p));
        };
        if (!exists(text_encoder_path) || !exists(unet_path) || !exists(vae_decoder_path)) {
            if (error) *error = "strict_model_loading requires existing text_encoder/unet/vae paths";
            return false;
        }
    }
    if (!allow_stub_inference && !strict_model_loading) {
        if (error) *error = "allow_stub_inference=false requires strict_model_loading=true";
        return false;
    }
    return true;
}

SdBaseConfig SdBaseConfig::FromPreset(SdQualityPreset preset) {
    SdBaseConfig cfg;
    switch (preset) {
        case SdQualityPreset::Draft:
            cfg.default_steps = 12;
            cfg.max_steps = 40;
            cfg.text_embedding_dim = 384;
            cfg.eta = 0.2f;
            cfg.enable_sdxl = false;
            cfg.enable_controlnet = false;
            cfg.allow_stub_inference = true;
            break;
        case SdQualityPreset::Balanced:
            cfg.default_steps = 30;
            cfg.max_steps = 120;
            cfg.text_embedding_dim = 768;
            cfg.eta = 0.0f;
            break;
        case SdQualityPreset::High:
            cfg.default_steps = 50;
            cfg.max_steps = 160;
            cfg.text_embedding_dim = 1024;
            cfg.eta = 0.0f;
            cfg.enable_sdxl = true;
            cfg.enable_controlnet = true;
            cfg.enable_vae = true;
                cfg.allow_stub_inference = false;
            break;
    }
    return cfg;
}

}  // namespace XER::AI::SDBase
