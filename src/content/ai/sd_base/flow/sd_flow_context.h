#pragma once

#include "content/ai/sd_base/core/sd_base_types.h"

#include <chrono>

namespace XER::AI::SDBase {

struct SdFlowContext {
    SdGenerationRequest request;
    SdRuntimeCapabilities caps;
    std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();

    long long ElapsedMs() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at).count();
    }
};

}  // namespace XER::AI::SDBase
