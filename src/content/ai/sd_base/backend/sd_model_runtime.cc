#include "content/ai/sd_base/backend/sd_model_runtime.h"

#include "helper/env_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace XER::AI::SDBase {

bool SdModelRuntime::Exists(const std::string& path) {
    return !path.empty() && std::filesystem::exists(std::filesystem::path(path));
}

namespace {

bool HasSuffixCI(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    return std::equal(suf.rbegin(), suf.rend(), s.rbegin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

}  // namespace

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

    latent_h_ = static_cast<int>(std::max<uint32_t>(1, request.height / 8));
    latent_w_ = static_cast<int>(std::max<uint32_t>(1, request.width / 8));
    unet_real_ = false;
    vae_path_.clear();
    if (!vae.empty() && HasSuffixCI(vae, ".safetensors")) {
        vae_path_ = vae;
    }

    // Real CLIP text encoder: safetensors weights + vocab.json/merges.txt
    // expected in the same directory as the checkpoint.
    clip_real_ = false;
    if (!text_encoder.empty() && HasSuffixCI(text_encoder, ".safetensors")) {
        const auto te_dir = std::filesystem::path(text_encoder).parent_path();
        std::string cerr;
        clip_.Load(text_encoder, te_dir.string(), &cerr);
        clip_real_ = clip_.IsLoaded();
        if (!clip_real_ && error_out && !cerr.empty()) {
            // non-fatal: falls back to deterministic embeddings
        }
    }

    // Real UNet weights: safetensors checkpoints load directly; .ckpt/.pth/.gguf
    // need conversion first (kept as existence-check only for now).
    if (!unet.empty() && HasSuffixCI(unet, ".safetensors")) {
        std::string wt_err;
        if (unet_.Load(unet, &wt_err)) {
            unet_real_ = unet_.IsLoaded();
            // Derive cross-attention context dim from attn2.to_k weight.
            if (const auto* tk = unet_.Weights().FindModel(
                    "mid_block.attentions.0.transformer_blocks.0."
                    "attn2.to_k.weight")) {
                if (tk->shape.size() >= 2) ctx_dim_ = static_cast<int>(tk->shape[1]);
            }
        }
    }

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
    // Real CLIP path: only when output dims match what the UNet expects.
    if (clip_real_ && clip_.ContextDim() == ctx_dim_ &&
        clip_.ContextLen() == ctx_len_) {
        auto emb = clip_.Encode(prompt);
        if (!emb.empty()) {
            if (!XER::Helper::GetEnv("SD_DEBUG_EMB").empty() && emb.size() >= 4) {
                std::fprintf(stderr, "[emb] %.4f %.4f %.4f %.4f\n",
                             emb[0], emb[1], emb[2], emb[3]);
            }
            return emb;
        }
    }
    if (unet_real_) {
        // Context-token layout expected by UNet cross-attention:
        // [ctx_len_, ctx_dim_] row-major (token-major). Deterministic
        // pseudo-embedding until a real text encoder is wired in.
        const size_t n = static_cast<size_t>(ctx_len_) * ctx_dim_;
        std::vector<float> emb(n, 0.0f);
        if (prompt.empty()) return emb;

        const uint64_t h = std::hash<std::string>{}(prompt);
        for (int tok = 0; tok < ctx_len_; ++tok) {
            const uint64_t th = h ^ (static_cast<uint64_t>(tok) *
                                     0x9E3779B97F4A7C15ULL);
            for (int d = 0; d < ctx_dim_; ++d) {
                const uint64_t bits = (th >> ((d + tok) % 32)) ^
                                      (static_cast<uint64_t>(d) *
                                       0xBF58476D1CE4E5B9ULL);
                const float v = static_cast<float>(bits & 0xFFFFu) / 65535.0f;
                emb[static_cast<size_t>(tok) * ctx_dim_ + d] = 2.0f * v - 1.0f;
            }
        }
        return emb;
    }

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
    PredictNoiseCfg(text_embedding, text_embedding, latent, element_count,
                    timestep, sigma, guidance);
}

void SdModelRuntime::PredictNoiseCfg(
    const std::vector<float>& cond_embedding,
    const std::vector<float>& uncond_embedding,
    float* latent,
    uint64_t element_count,
    uint32_t timestep,
    float sigma,
    float guidance) const {
    if (latent == nullptr || element_count == 0) return;
    if (cond_embedding.empty()) return;

    if (!unet_real_) {
        PredictNoiseSynthetic(cond_embedding, latent, element_count,
                              timestep, sigma, guidance);
        return;
    }

    // Real UNet forward. `latent` buffer holds x_t on entry (pipeline copies
    // latent -> eps buffer); we overwrite it with the CFG-combined eps.
    const int h = latent_h_ > 0 ? latent_h_ : 64;
    const int w = latent_w_ > 0 ? latent_w_ : 64;
    const size_t need = static_cast<size_t>(4) * h * w;
    if (element_count < need) return;

    const float t_float = std::clamp(sigma, 0.0f, 1.0f) * 999.0f;
    const float g = std::clamp(guidance, 0.0f, 30.0f);

    std::vector<float> eps_cond(need, 0.0f);
    std::vector<float> eps_uncond(need, 0.0f);

    const float* ctx_c = cond_embedding.data();
    const float* ctx_u = uncond_embedding.empty() ? nullptr
                                                  : uncond_embedding.data();
    const int ctx_len = ctx_len_;
    const int ctx_dim = ctx_dim_;

    unet_.Forward(latent, h, w, ctx_c, ctx_len, ctx_dim, t_float,
                  eps_cond.data());
    unet_.Forward(latent, h, w, ctx_u, ctx_len, ctx_dim, t_float,
                  eps_uncond.data());

    for (size_t i = 0; i < need; ++i) {
        latent[i] = eps_uncond[i] + g * (eps_cond[i] - eps_uncond[i]);
    }
    if (!XER::Helper::GetEnv("SD_DEBUG_EPS").empty() && need > 0) {
        std::fprintf(stderr, "[eps] %.5f %.5f %.5f %.5f\n",
                     eps_cond[0], eps_cond[1], eps_uncond[0], latent[0]);
    }
    (void)timestep;
}

void SdModelRuntime::PredictNoiseSynthetic(
    const std::vector<float>& text_embedding,
    float* latent,
    uint64_t element_count,
    uint32_t timestep,
    float sigma,
    float guidance) const {
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
