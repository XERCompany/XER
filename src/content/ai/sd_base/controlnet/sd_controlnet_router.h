#pragma once

#include "content/ai/ml/data_types/tensor.h"

namespace XER::AI::SDBase {

class SdControlNetRouter {
public:
    void ApplyHintScale(XER::MLData::Types::Tensor& latent, float hint_strength) const;
};

}  // namespace XER::AI::SDBase
