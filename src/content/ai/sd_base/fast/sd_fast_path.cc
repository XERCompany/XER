#include "content/ai/sd_base/fast/sd_fast_path.h"

#include <algorithm>

namespace XER::AI::SDBase {

uint32_t SdFastPath::ResolveStepCount(const SdGenerationRequest& request) {
    uint32_t steps = request.steps;
    if (request.preset == SdQualityPreset::Draft) {
        steps = std::min<uint32_t>(steps, 16);
    }
    if (request.width * request.height <= 512u * 512u) {
        steps = std::min<uint32_t>(steps, 40);
    }
    return std::max<uint32_t>(1, steps);
}

}  // namespace XER::AI::SDBase
