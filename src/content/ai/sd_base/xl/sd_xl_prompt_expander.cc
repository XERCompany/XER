#include "content/ai/sd_base/xl/sd_xl_prompt_expander.h"

namespace XER::AI::SDBase {

std::string SdXlPromptExpander::Expand(const std::string& prompt) const {
    if (prompt.empty()) {
        return "masterpiece, highly detailed, cinematic lighting";
    }
    return prompt + ", ultra detailed, sharp focus, volumetric lighting";
}

}  // namespace XER::AI::SDBase
