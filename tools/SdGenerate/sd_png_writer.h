#pragma once

#include "content/ai/ml/data_types/tensor.h"

#include <cstdint>
#include <map>
#include <string>

namespace XER::Tools::SdGenerate {

// Write an RGBA8 PNG (8-bit, color type 6) encoded with zlib.
// |parameters| is embedded as a tEXt chunk in AUTOMATIC1111 webui format:
//   "<prompt>\nNegative prompt: <neg>\nSteps: N, Sampler: ..., ..."
// Returns true on success; |error| receives a description on failure.
bool WritePng(const uint8_t* rgba,
              uint32_t width,
              uint32_t height,
              const std::string& path,
              const std::map<std::string, std::string>& metadata,
              const std::string& prompt,
              const std::string& negative_prompt,
              const std::string& sampler_name,
              std::string* error);

// Convert an SD image tensor {1,3,H,W} float RGB [0,1] to RGBA8 pixels.
// Returns false when the tensor shape/type is unsupported.
bool TensorToRgba8(const XER::MLData::Types::Tensor& image,
                   std::vector<uint8_t>* out_rgba,
                   uint32_t* out_width,
                   uint32_t* out_height);

}  // namespace XER::Tools::SdGenerate
