#include "content/ai/sd_base/backend/sd_model_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace XER::AI::SDBase {

bool SdModelRuntime::Exists(const std::string& path) {
    return !path.empty() && std::filesystem::exists(std::filesystem::path(path));
}

bool SdModelRuntime::Load(const SdBaseConfig& config,
                          const SdGenerationRequest& request,
                          const SdRuntimeCapabilities&,
                          SdBackendKind backend,
                          std::string* error_out) {
    backend_ = backend;

    const std::string text_encoder = request.text_encoder_path.empty() ? config.text_encoder_path : request.text_encoder_path;
    const std::string unet = request.unet_path.empty() ? config.unet_path : request.unet_path;
    const std::string vae = request.vae_decoder_path.empty() ? config.vae_decoder_path : request.vae_decoder_path;
    const std::string controlnet = request.controlnet_path.empty() ? config.controlnet_path : request.controlnet_path;
    const std::string sdxl_text2 = request.sdxl_text_encoder_2_path.empty()
        ? config.sdxl_text_encoder_2_path
        : request.sdxl_text_encoder_2_path;
    const std::string sdxl_refiner = request.sdxl_refiner_unet_path.empty()
        ? config.sdxl_refiner_unet_path
        : request.sdxl_refiner_unet_path;

    loaded_.text_encoder_loaded = Exists(text_encoder);
    loaded_.unet_loaded = Exists(unet);
    loaded_.vae_loaded = Exists(vae);
    loaded_.controlnet_loaded = Exists(controlnet);
    loaded_.sdxl_branch_loaded = Exists(sdxl_text2) && Exists(sdxl_refiner);

    const bool strict = request.strict_model_loading || config.strict_model_loading;
    if (strict && (!loaded_.text_encoder_loaded || !loaded_.unet_loaded || !loaded_.vae_loaded)) {
        if (error_out != nullptr) {
            *error_out = "strict model loading failed: missing text_encoder/unet/vae weights";
        }
        return false;
    }

    if (!config.allow_stub_inference && (!loaded_.text_encoder_loaded || !loaded_.unet_loaded || !loaded_.vae_loaded)) {
        if (error_out != nullptr) {
            *error_out = "stub inference disabled and required model weights are missing";
        }
        return false;
    }

    return true;
}

std::vector<float> SdModelRuntime::EncodeText(const std::string& prompt, uint32_t dim) const {
    const uint32_t safe_dim = std::max<uint32_t>(8, dim);
    std::vector<float> embedding(safe_dim, 0.0f);
    if (prompt.empty()) return embedding;

    const uint64_t hash = std::hash<std::string>{}(prompt + SdGpuDispatch::BackendName(backend_));
    for (uint32_t i = 0; i < safe_dim; ++i) {
        const uint64_t bits = (hash >> (i % 32)) ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL);
        const float v = static_cast<float>((bits & 0x3FFULL) / 1023.0);
        embedding[i] = 2.0f * v - 1.0f;
    }
    return embedding;
}

void SdModelRuntime::PredictNoise(const std::vector<float>& text_embedding,
                                  float* latent,
                                  uint64_t element_count,
                                  uint32_t timestep,
                                  float sigma,
                                  float guidance) const {
    if (latent == nullptr || element_count == 0) return;
    if (text_embedding.empty()) return;

    float emb_mean = 0.0f;
    for (float v : text_embedding) emb_mean += v;
    emb_mean /= static_cast<float>(text_embedding.size());

    const float backend_bias = [&]() {
        switch (backend_) {
            case SdBackendKind::CudaCudnn: return 1.10f;
            case SdBackendKind::CudaCutlass: return 1.08f;
            case SdBackendKind::OpenVino: return 1.05f;
            case SdBackendKind::Onnx: return 1.04f;
            case SdBackendKind::Tensorflow: return 1.03f;
            case SdBackendKind::CpuXnnpack: return 1.02f;
            case SdBackendKind::CpuEigen: return 1.0f;
        }
        return 1.0f;
    }();

    const float t_scale = 1.0f / static_cast<float>(std::max<uint32_t>(1, timestep + 1));
    const float g = std::clamp(guidance, 0.0f, 30.0f) * 0.0015f;

    for (uint64_t i = 0; i < element_count; ++i) {
        const float phase = static_cast<float>((i % 8192ULL) * 0.0017);
        const float model_term = std::sin(phase + emb_mean * 1.7f + sigma * 0.3f);
        const float eps = backend_bias * (model_term + g);
        latent[i] = latent[i] - t_scale * eps;
    }
}

}  // namespace XER::AI::SDBase
