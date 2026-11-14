#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace XER::AI::SDBase {

// One tensor decoded to float32 (F16 values are converted on load).
struct SdWeightTensor {
    std::string name;
    std::vector<int64_t> shape;   // dims as stored in the file
    std::vector<float> data;      // row-major, numel = prod(shape)
    std::string dtype;            // original dtype tag ("F32", "F16", ...)

    int64_t Dim(size_t i) const {
        return i < shape.size() ? shape[i] : 1;
    }
    size_t Numel() const { return data.size(); }
};

// Read-only view over a safetensors checkpoint.
// Supports F32 and F16 dtypes; other dtypes are skipped and counted.
class SdWeightSet {
public:
    // Load a .safetensors file. Returns false + |error| on malformed input.
    bool Load(const std::string& path, std::string* error);

    bool IsLoaded() const { return !tensors_.empty(); }
    size_t Size() const { return tensors_.size(); }
    uint32_t SkippedTensors() const { return skipped_; }

    const SdWeightTensor* Find(const std::string& name) const;
    // Find with common checkpoint prefixes stripped
    // ("unet.", "model.diffusion_model.", "diffusion_model.").
    const SdWeightTensor* FindModel(const std::string& name) const;

    const std::map<std::string, SdWeightTensor>& Tensors() const {
        return tensors_;
    }

private:
    std::map<std::string, SdWeightTensor> tensors_;
    uint32_t skipped_ = 0;
};

}  // namespace XER::AI::SDBase
