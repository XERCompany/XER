#pragma once

#include "content/ai/sd_base/core/sd_base_types.h"

namespace XER::AI::SDBase {

class SdFastPath {
public:
    static uint32_t ResolveStepCount(const SdGenerationRequest& request);
};

}  // namespace XER::AI::SDBase
