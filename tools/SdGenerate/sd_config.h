#pragma once

#include "content/ai/sd_base/core/sd_base_config.h"
#include "content/ai/sd_base/core/sd_base_types.h"

#include <cstdint>
#include <string>

namespace XER::Tools::SdGenerate {

// Fully-resolved CLI configuration parsed from generate.json.
struct SdCliConfig {
    // Generation request (prompt, size, steps, seed, toggles...).
    XER::AI::SDBase::SdGenerationRequest request;

    // Pipeline-level knobs (limits, embedding dim, model paths...).
    XER::AI::SDBase::SdBaseConfig base;

    // Raw "model" field — may be a local path/dir or a Civitai model name.
    std::string model_spec;
    // Explicit Civitai ids when the user pins a version instead of searching.
    std::string civitai_model_id;
    std::string civitai_version_id;
    // Optional Civitai API token (XER_CIVITAI_API_KEY env is the default).
    std::string api_key;

    // Output image path (default: sd_<seed>.png next to the JSON file).
    std::string output_path;
    // Local weights directory override (contains unet/text_encoder/vae files).
    std::string model_dir;

    // Informational sampler label for PNG metadata.
    std::string sampler_name = "euler";
    bool embed_metadata = true;

    // Batch count (1 = single image).
    uint32_t count = 1;
    // Post-generation integer upscale factor (1 = off, via OpenCV).
    uint32_t upscale = 1;
    // Optional LoRA adapter (.safetensors) merged into the UNet weights.
    std::string lora_path;
    float lora_strength = 1.0f;
};

// Parse generate.json into SdCliConfig. Returns false + fills |error| on
// malformed JSON or missing required fields ("prompt" / "model").
bool LoadSdConfig(const std::string& json_path,
                  SdCliConfig* out,
                  std::string* error);

}  // namespace XER::Tools::SdGenerate
