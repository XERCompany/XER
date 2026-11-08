#include "content/ai/sd_base/xl/sd_xl_branch.h"

#include "content/ai/sd_base/xl/sd_xl_prompt_expander.h"

namespace XER::AI::SDBase {

std::string SdXlBranch::BuildPrompt(const SdGenerationRequest& request,
                                    SdBackendKind backend,
                                    bool model_loaded) const {
    SdXlPromptExpander expander;
    std::string prompt = request.prompt;

    if (request.enable_sdxl) {
        prompt = expander.Expand(prompt);
        if (model_loaded) {
            prompt += ", sdxl dual-encoder guidance";
        }
    }

    switch (backend) {
        case SdBackendKind::CudaCudnn:
        case SdBackendKind::CudaCutlass:
            prompt += ", high frequency detail";
            break;
        case SdBackendKind::OpenVino:
        case SdBackendKind::Onnx:
            prompt += ", optimized inference path";
            break;
        default:
            break;
    }

    return prompt;
}

float SdXlBranch::RefinerGain(SdBackendKind backend, bool model_loaded) const {
    float gain = model_loaded ? 1.08f : 1.02f;
    switch (backend) {
        case SdBackendKind::CudaCudnn: gain *= 1.03f; break;
        case SdBackendKind::CudaCutlass: gain *= 1.02f; break;
        case SdBackendKind::OpenVino: gain *= 1.01f; break;
        default: break;
    }
    return gain;
}

}  // namespace XER::AI::SDBase
