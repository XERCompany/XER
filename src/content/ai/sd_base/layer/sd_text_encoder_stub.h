#pragma once

#include "content/ai/ml/data_types/tensor.h"

#include <string>

namespace XER::AI::SDBase {

class SdTextEncoderStub {
public:
    XER::MLData::Types::Tensor Encode(const std::string& prompt,
                                         uint32_t max_tokens,
                                         uint32_t embedding_dim) const;
};

}  // namespace XER::AI::SDBase
