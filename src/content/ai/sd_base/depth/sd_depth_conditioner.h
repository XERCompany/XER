#pragma once

#include "content/ai/ml/data_types/tensor.h"

namespace XER::AI::SDBase {

class SdDepthConditioner {
public:
    void ApplyDepthPrior(XER::MLData::Types::Tensor& latent, float depth_weight) const;
};

}  // namespace XER::AI::SDBase
