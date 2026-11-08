#pragma once

#include "content/ai/ml/data_types/tensor.h"

namespace XER::AI::SDBase {

class SdVaeDecoder {
public:
    XER::MLData::Types::Tensor Decode(const XER::MLData::Types::Tensor& latent,
                                         uint32_t width,
                                         uint32_t height,
                                         bool high_quality) const;
};

}  // namespace XER::AI::SDBase
