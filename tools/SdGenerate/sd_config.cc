#include "sd_config.h"

#include "helper/string_utils.h"

#include <json/json.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace XER::Tools::SdGenerate {

namespace {

using XER::AI::SDBase::SdQualityPreset;
using XER::AI::SDBase::SdSchedulerType;

bool JsonToValue(const std::string& path, Json::Value* out, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        *error = "cannot open config: " + path;
        return false;
    }
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, f, out, &errs)) {
        *error = "JSON parse error in " + path + ": " + errs;
        return false;
    }
    return true;
}

std::string Str(const Json::Value& obj, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const Json::Value& v = obj[k];
        if (v.isString() && !v.asString().empty()) return v.asString();
    }
    return {};
}

int64_t Int(const Json::Value& obj, std::initializer_list<const char*> keys,
            int64_t fallback) {
    for (const char* k : keys) {
        const Json::Value& v = obj[k];
        if (v.isInt() || v.isInt64() || v.isUInt() || v.isUInt64()) {
            return v.asInt64();
        }
        if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
        if (v.isString()) {
            char* end = nullptr;
            const long long parsed = std::strtoll(v.asString().c_str(), &end, 10);
            if (end && *end == '\0') return static_cast<int64_t>(parsed);
        }
    }
    return fallback;
}

double Num(const Json::Value& obj, std::initializer_list<const char*> keys,
           double fallback) {
    for (const char* k : keys) {
        const Json::Value& v = obj[k];
        if (v.isNumeric()) return v.asDouble();
        if (v.isString()) {
            char* end = nullptr;
            const double parsed = std::strtod(v.asString().c_str(), &end);
            if (end && *end == '\0') return parsed;
        }
    }
    return fallback;
}

bool Bool(const Json::Value& obj, std::initializer_list<const char*> keys,
          bool fallback) {
    for (const char* k : keys) {
        const Json::Value& v = obj[k];
        if (v.isBool()) return v.asBool();
        if (v.isInt() || v.isInt64()) return v.asInt64() != 0;
        if (v.isString()) {
            const std::string s = XER::Helper::String::ToLowerAscii(v.asString());
            if (s == "true" || s == "1" || s == "yes") return true;
            if (s == "false" || s == "0" || s == "no") return false;
        }
    }
    return fallback;
}

SdQualityPreset ParsePreset(const std::string& s) {
    const std::string p = XER::Helper::String::ToLowerAscii(s);
    if (p == "draft") return SdQualityPreset::Draft;
    if (p == "high" || p == "hq" || p == "quality") return SdQualityPreset::High;
    return SdQualityPreset::Balanced;
}

SdSchedulerType ParseScheduler(const std::string& s) {
    const std::string v = XER::Helper::String::ToLowerAscii(s);
    if (v == "ddim") return SdSchedulerType::DDIM;
    return SdSchedulerType::Euler;
}

}  // namespace

bool LoadSdConfig(const std::string& json_path, SdCliConfig* out,
                  std::string* error) {
    Json::Value root;
    if (!JsonToValue(json_path, &root, error) || !root.isObject()) {
        if (error->empty()) *error = "config root is not a JSON object";
        return false;
    }

    SdCliConfig cfg;
    auto& req = cfg.request;
    auto& base = cfg.base;

    cfg.model_spec = Str(root, {"model", "model_name", "checkpoint"});
    cfg.civitai_model_id = Str(root, {"civitai_model_id", "model_id"});
    cfg.civitai_version_id =
        Str(root, {"civitai_version_id", "civitai_model_version_id",
                   "model_version_id"});
    cfg.api_key = Str(root, {"api_key", "civitai_api_key", "token"});
    cfg.model_dir = Str(root, {"model_dir", "weights_dir", "model_root"});
    cfg.output_path = Str(root, {"output", "output_path", "out", "save_as"});
    cfg.sampler_name = Str(root, {"sampler", "sampler_name"});
    if (cfg.sampler_name.empty()) cfg.sampler_name = "euler";
    cfg.embed_metadata = Bool(root, {"embed_metadata", "metadata"}, true);
    cfg.count = static_cast<uint32_t>(
        std::max<int64_t>(1, Int(root, {"count", "n", "batch", "num_images"}, 1)));
    cfg.upscale = static_cast<uint32_t>(
        std::clamp<int64_t>(Int(root, {"upscale", "upscale_factor"}, 1), 1, 8));
    cfg.lora_path = Str(root, {"lora", "lora_path", "lora_model"});
    cfg.lora_strength = static_cast<float>(
        Num(root, {"lora_strength", "lora_scale"}, 1.0));

    req.prompt = Str(root, {"prompt", "text", "txt"});
    req.negative_prompt =
        Str(root, {"negative_prompt", "negative", "neg"});
    if (req.prompt.empty()) {
        *error = "missing required field: \"prompt\"";
        return false;
    }
    if (cfg.model_spec.empty() && cfg.civitai_model_id.empty() &&
        cfg.model_dir.empty()) {
        *error = "missing required field: \"model\" (name, path or Civitai id)";
        return false;
    }

    req.width = static_cast<uint32_t>(
        std::max<int64_t>(64, Int(root, {"size_w", "width", "w"}, 512)));
    req.height = static_cast<uint32_t>(
        std::max<int64_t>(64, Int(root, {"size_h", "height", "h"}, 512)));
    req.steps = static_cast<uint32_t>(
        std::max<int64_t>(1, Int(root, {"steps", "num_inference_steps"}, 30)));
    req.guidance_scale = static_cast<float>(
        Num(root, {"guidance", "guidance_scale", "cfg", "cfg_scale"}, 7.5));

    // seed: -1 / "random" / absent -> 0 (pipeline derives a time+prompt seed).
    const int64_t seed = Int(root, {"seed", "seed_value"}, -1);
    req.seed = seed > 0 ? static_cast<uint64_t>(seed) : 0;

    const std::string sched = Str(root, {"scheduler", "scheduler_type"});
    req.scheduler = ParseScheduler(sched);
    req.preset = ParsePreset(Str(root, {"preset", "quality"}));

    req.enable_sdxl = Bool(root, {"enable_sdxl", "sdxl"}, true);
    req.enable_controlnet =
        Bool(root, {"enable_controlnet", "controlnet"}, false);
    req.enable_vae_decode = Bool(root, {"enable_vae", "vae"}, true);
    req.controlnet_strength = static_cast<float>(
        Num(root, {"controlnet_strength", "control_strength"}, 0.05));
    req.controlnet_hint = Str(root, {"controlnet_hint", "control_image"});
    req.backend_hint = Str(root, {"backend", "backend_hint"});

    base.text_embedding_dim = static_cast<uint32_t>(
        Int(root, {"text_embedding_dim", "clip_dim"}, 768));
    base.init_noise_sigma = static_cast<float>(
        Num(root, {"init_noise_sigma", "sigma"}, 1.0));
    base.eta = static_cast<float>(Num(root, {"eta"}, 0.0));
    base.max_steps = static_cast<uint32_t>(
        Int(root, {"max_steps"}, 150));
    base.default_steps = req.steps;

    // Per-request weight path overrides (absolute or relative paths).
    req.text_encoder_path = Str(root, {"text_encoder", "text_encoder_path"});
    req.unet_path = Str(root, {"unet", "unet_path", "diffusion_model"});
    req.vae_decoder_path = Str(root, {"vae", "vae_path", "vae_decoder"});
    req.controlnet_path = Str(root, {"controlnet_path"});
    req.sdxl_text_encoder_2_path =
        Str(root, {"sdxl_text_encoder_2", "sdxl_text_encoder_2_path"});
    req.sdxl_refiner_unet_path =
        Str(root, {"sdxl_refiner_unet", "sdxl_refiner_unet_path"});
    req.strict_model_loading =
        Bool(root, {"strict_model_loading", "strict"}, false);

    *out = std::move(cfg);
    return true;
}

}  // namespace XER::Tools::SdGenerate
