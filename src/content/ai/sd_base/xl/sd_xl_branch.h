#pragma once

#include "content/ai/sd_base/core/sd_base_types.h"
#include "content/ai/sd_base/gpu/sd_gpu_dispatch.h"

#include <string>

namespace XER::AI::SDBase {

class SdXlBranch {
public:
    std::string BuildPrompt(const SdGenerationRequest& request,
                            SdBackendKind backend,
                            bool model_loaded) const;

    float RefinerGain(SdBackendKind backend, bool model_loaded) const;
};

}  // namespace XER::AI::SDBase
