#pragma once

#include "content/ai/sd_base/core/sd_base_config.h"
#include "content/ai/sd_base/core/sd_base_types.h"

#include <cstdint>
#include <string>

namespace XER::AI::SDBase {

enum class SdBackendKind : uint8_t {
    CpuEigen = 0,
    CpuXnnpack,
    CudaCudnn,
    CudaCutlass,
    OpenVino,
    Onnx,
    Tensorflow,
    Vulkan,
    LevelZero,
    Ggml,
};

class SdGpuDispatch {
public:
    static SdRuntimeCapabilities Detect();
    static bool ShouldUseCuda(const SdRuntimeCapabilities& caps, bool prefer_cuda);
    static SdBackendKind SelectBackend(const SdRuntimeCapabilities& caps,
                                       const SdGenerationRequest& request,
                                       const SdBaseConfig& config);
    static std::string BackendName(SdBackendKind backend);
};

}  // namespace XER::AI::SDBase
