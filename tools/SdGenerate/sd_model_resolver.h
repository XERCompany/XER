#pragma once

#include "sd_config.h"

#include <string>

namespace XER::Tools::SdGenerate {

// Result of resolving the "model" field into concrete weight file paths.
struct ResolvedModel {
    bool ok = false;
    std::string error;
    // Human-readable model name for PNG metadata.
    std::string display_name;
    // Downloaded/located weight files (empty = use config defaults).
    std::string unet_path;
    std::string text_encoder_path;
    std::string vae_decoder_path;
    std::string controlnet_path;
    std::string sdxl_text_encoder_2_path;
    std::string sdxl_refiner_unet_path;
    // Local weights directory (also used as fallback for sibling files).
    std::string model_dir;
    // True when files were fetched from Civitai during this run.
    bool downloaded = false;
};

// Resolve cfg.model_spec:
//  * existing filesystem path (file or directory) -> local weights
//  * civitai_model_id / civitai_version_id -> direct Civitai download
//  * otherwise -> Civitai name search -> download best match
// On success fills |out| and applies paths onto cfg.request weight fields.
ResolvedModel ResolveModel(const SdCliConfig& cfg);

}  // namespace XER::Tools::SdGenerate
