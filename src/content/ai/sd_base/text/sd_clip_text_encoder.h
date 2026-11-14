#pragma once

#include "../weights/sd_safetensors.h"
#include "sd_clip_tokenizer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::AI::SDBase {

// Real CLIP text encoder (diffusers CLIPTextModel layout) producing the
// [77, hidden] last_hidden_state used as UNet cross-attention context.
class SdClipTextEncoder {
public:
    // path: text_encoder .safetensors checkpoint.
    // tok_dir: directory containing vocab.json + merges.txt (may equal
    //          the checkpoint's directory; empty disables tokenization).
    bool Load(const std::string& path, const std::string& tok_dir,
              std::string* error);
    bool IsLoaded() const { return weights_.IsLoaded() && tok_.IsLoaded(); }

    int ContextLen() const { return ctx_len_; }
    int ContextDim() const { return ctx_dim_; }

    // Returns [ctx_len * ctx_dim] token-major floats (token, dim order),
    // matching what SdUnet::Forward expects via SdModelRuntime::EncodeText.
    // Empty vector on failure.
    std::vector<float> Encode(const std::string& prompt) const;

private:
    const SdWeightTensor* W(const std::string& name) const;

    SdWeightSet weights_;
    SdClipTokenizer tok_;
    int ctx_len_ = 77;
    int ctx_dim_ = 768;
    int layers_ = 12;
    int heads_ = 12;
};

}  // namespace XER::AI::SDBase
