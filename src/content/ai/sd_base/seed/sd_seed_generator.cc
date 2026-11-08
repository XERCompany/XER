#include "content/ai/sd_base/seed/sd_seed_generator.h"

#include <chrono>
#include <functional>

namespace XER::AI::SDBase {

uint64_t SdSeedGenerator::FromPrompt(const std::string& prompt,
                                     const std::string& negative_prompt,
                                     uint64_t fallback_seed) {
    if (fallback_seed != 0) {
        return fallback_seed;
    }

    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const uint64_t h1 = std::hash<std::string>{}(prompt);
    const uint64_t h2 = std::hash<std::string>{}(negative_prompt);
    uint64_t mixed = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    mixed ^= now;
    return mixed == 0 ? 1 : mixed;
}

}  // namespace XER::AI::SDBase
