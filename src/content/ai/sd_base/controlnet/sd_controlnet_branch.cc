#include "content/ai/sd_base/controlnet/sd_controlnet_branch.h"

#include "content/ai/sd_base/controlnet/sd_controlnet_router.h"
#include "content/ai/sd_base/depth/sd_depth_conditioner.h"

#include <algorithm>

namespace XER::AI::SDBase {

void SdControlNetBranch::Apply(XER::MLData::Types::Tensor& latent,
                               const SdGenerationRequest& request,
                               SdBackendKind backend,
                               bool model_loaded) const {
    SdControlNetRouter router;
    SdDepthConditioner depth;

    float strength = request.controlnet_strength;
    float depth_strength = request.depth_strength;

    if (!model_loaded) {
        strength *= 0.5f;
        depth_strength *= 0.5f;
    }

    switch (backend) {
        case SdBackendKind::CudaCudnn:
        case SdBackendKind::CudaCutlass:
            strength *= 1.05f;
            break;
        case SdBackendKind::OpenVino:
        case SdBackendKind::Onnx:
            strength *= 1.02f;
            break;
        default:
            break;
    }

    const std::string hint = request.controlnet_hint;
    if (hint == "depth") {
        depth_strength *= 1.25f;
    } else if (hint == "canny") {
        strength *= 1.15f;
    }

    router.ApplyHintScale(latent, std::clamp(strength, 0.0f, 2.0f));
    depth.ApplyDepthPrior(latent, std::clamp(depth_strength, 0.0f, 1.0f));
}

}  // namespace XER::AI::SDBase
