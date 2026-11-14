#pragma once

#include <cstdint>
#include <string>

namespace XER::Tools::SdGenerate {

// Merge a LoRA adapter (safetensors) into a base checkpoint (safetensors).
// Computes W' = W + scale * (B @ A) for every lora_A/lora_B (or
// lora_down/lora_up) pair; non-LoRA tensors are copied verbatim.
// F32 tensors only — F16/BF16 tensors are copied unchanged and counted in
// |skipped_tensors|. Returns false + |error| on malformed input.
// On success writes a complete safetensors file to |out_path|.
bool MergeLoraSafetensors(const std::string& base_path,
                          const std::string& lora_path,
                          float scale,
                          const std::string& out_path,
                          uint32_t* merged_pairs,
                          uint32_t* skipped_tensors,
                          std::string* error);

}  // namespace XER::Tools::SdGenerate
