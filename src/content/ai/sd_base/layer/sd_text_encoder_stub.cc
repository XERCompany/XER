#include "content/ai/sd_base/layer/sd_text_encoder_stub.h"

#include <functional>

namespace XER::AI::SDBase {

XER::MLData::Types::Tensor SdTextEncoderStub::Encode(const std::string& prompt,
                                                        uint32_t max_tokens,
                                                        uint32_t embedding_dim) const {
    XER::MLData::Types::Tensor out({1, max_tokens, embedding_dim},
                                      XER::MLData::Types::DataType::FLOAT32);
    if (!out.GetData()) return out;

    float* d = static_cast<float*>(out.GetData());
    const uint64_t n = out.GetElementCount();
    const uint64_t h = std::hash<std::string>{}(prompt);

    for (uint64_t i = 0; i < n; ++i) {
        const uint64_t x = h ^ (i * 0x9e3779b97f4a7c15ULL);
        d[i] = static_cast<float>((x & 0xFFFFULL) / 32768.0 - 1.0);
    }
    return out;
}

}  // namespace XER::AI::SDBase
