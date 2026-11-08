#pragma once

#include "content/ai/ml/data_types/tensor.h"

namespace XER::AI::SDBase {

class SdImageDecoder {
public:
    XER::MLData::Types::Tensor DecodeLatentToImage(const XER::MLData::Types::Tensor& latent,
                                                      uint32_t width,
                                                      uint32_t height) const;
};

}  // namespace XER::AI::SDBase
