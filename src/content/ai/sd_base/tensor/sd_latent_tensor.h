#pragma once

#include "content/ai/ml/data_types/tensor.h"
#include "content/ai/ml/utils/ml_random_generator.h"

#include <cstdint>

namespace XER::AI::SDBase {

class SdLatentTensor {
public:
    static XER::MLData::Types::Tensor Make(uint32_t width, uint32_t height);
    static void FillGaussian(XER::MLData::Types::Tensor& latent,
                             XER::ML::Utils::MlRandomGenerator& rng,
                             float sigma);
};

}  // namespace XER::AI::SDBase
