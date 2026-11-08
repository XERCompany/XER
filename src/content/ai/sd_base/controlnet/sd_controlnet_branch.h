#pragma once

#include "content/ai/ml/data_types/tensor.h"
#include "content/ai/sd_base/core/sd_base_types.h"
#include "content/ai/sd_base/gpu/sd_gpu_dispatch.h"

namespace XER::AI::SDBase {

class SdControlNetBranch {
public:
    void Apply(XER::MLData::Types::Tensor& latent,
               const SdGenerationRequest& request,
               SdBackendKind backend,
               bool model_loaded) const;
};

}  // namespace XER::AI::SDBase
