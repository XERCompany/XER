#include "content/ai/sd_base/gpu/sd_gpu_dispatch.h"

#include "content/ai/models_builder/utility/ai_runtime_features.h"
#include "wrapper/cuda/cuda_engine_bridge.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/opencv/opencv_engine_bridge.h"

#include <algorithm>
#include <cctype>

namespace {

std::string LowerCopy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

}  // namespace

namespace XER::AI::SDBase {

SdRuntimeCapabilities SdGpuDispatch::Detect() {
    SdRuntimeCapabilities caps;
    const auto libs = XER::ModelsBuilder::Utility::DetectExternalLibraries();

    caps.cuda_available = libs.has_cuda && XER::Bridge::Cuda::IsAvailable();
    caps.cudnn_available = libs.has_cudnn;
    caps.cutlass_available = libs.has_cutlass;
    caps.openvino_available = libs.has_openvino;
    caps.onnx_available = libs.has_onnx;
    caps.tensorflow_available = libs.has_tensorflow;
    caps.opencv_available = libs.has_opencv && XER::Bridge::OpenCV::IsAvailable();
    caps.ffmpeg_available = XER::Bridge::FFmpeg::IsAvailable();
    caps.eigen_available = libs.has_eigen;
    caps.xnnpack_available = libs.has_xnnpack;
    caps.selected_backend = "cpu/eigen";
    return caps;
}

bool SdGpuDispatch::ShouldUseCuda(const SdRuntimeCapabilities& caps, bool prefer_cuda) {
    return prefer_cuda && caps.cuda_available;
}

std::string SdGpuDispatch::BackendName(SdBackendKind backend) {
    switch (backend) {
        case SdBackendKind::CudaCudnn: return "cuda/cudnn";
        case SdBackendKind::CudaCutlass: return "cuda/cutlass";
        case SdBackendKind::OpenVino: return "openvino";
        case SdBackendKind::Onnx: return "onnx";
        case SdBackendKind::CpuXnnpack: return "cpu/xnnpack";
        case SdBackendKind::Tensorflow: return "tensorflow";
        case SdBackendKind::CpuEigen: return "cpu/eigen";
    }
    return "cpu/eigen";
}

SdBackendKind SdGpuDispatch::SelectBackend(const SdRuntimeCapabilities& caps,
                                           const SdGenerationRequest& request,
                                           const SdBaseConfig& config) {
    const std::string hint = LowerCopy(request.backend_hint);
    if (hint == "cuda" || hint == "cudnn" || hint == "cuda/cudnn") {
        if (caps.cuda_available && caps.cudnn_available) return SdBackendKind::CudaCudnn;
    }
    if (hint == "cutlass" || hint == "cuda/cutlass") {
        if (caps.cuda_available && caps.cutlass_available) return SdBackendKind::CudaCutlass;
    }
    if (hint == "openvino" && caps.openvino_available) return SdBackendKind::OpenVino;
    if (hint == "onnx" && caps.onnx_available) return SdBackendKind::Onnx;
    if ((hint == "xnn" || hint == "xnnpack") && caps.xnnpack_available) return SdBackendKind::CpuXnnpack;
    if ((hint == "tf" || hint == "tensorflow") && caps.tensorflow_available) return SdBackendKind::Tensorflow;
    if (hint == "eigen" && caps.eigen_available) return SdBackendKind::CpuEigen;

    if (config.prefer_cuda && config.prefer_cudnn && caps.cuda_available && caps.cudnn_available) {
        return SdBackendKind::CudaCudnn;
    }
    if (config.prefer_cuda && config.prefer_cutlass && caps.cuda_available && caps.cutlass_available) {
        return SdBackendKind::CudaCutlass;
    }
    if (config.prefer_openvino && caps.openvino_available) {
        return SdBackendKind::OpenVino;
    }
    if (config.prefer_onnx && caps.onnx_available) {
        return SdBackendKind::Onnx;
    }
    if (config.prefer_xnnpack && caps.xnnpack_available) {
        return SdBackendKind::CpuXnnpack;
    }
    if (config.prefer_tensorflow && caps.tensorflow_available) {
        return SdBackendKind::Tensorflow;
    }
    if (config.prefer_eigen && caps.eigen_available) {
        return SdBackendKind::CpuEigen;
    }

    if (caps.eigen_available) return SdBackendKind::CpuEigen;
    if (caps.xnnpack_available) return SdBackendKind::CpuXnnpack;
    if (caps.onnx_available) return SdBackendKind::Onnx;
    if (caps.openvino_available) return SdBackendKind::OpenVino;
    if (caps.tensorflow_available) return SdBackendKind::Tensorflow;
    if (caps.cuda_available && caps.cudnn_available) return SdBackendKind::CudaCudnn;
    if (caps.cuda_available && caps.cutlass_available) return SdBackendKind::CudaCutlass;

    return SdBackendKind::CpuEigen;
}

}  // namespace XER::AI::SDBase
