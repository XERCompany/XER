#include "ai_runtime_features.h"

#include <algorithm>
#include <array>
#include <thread>

#include "../../../../config.h"

#if __has_include(<absl/strings/str_cat.h>)
#include <absl/strings/str_cat.h>
#define XER_AI_HAS_ABSL_HEADER 1
#else
#define XER_AI_HAS_ABSL_HEADER 0
#endif

#if __has_include(<range/v3/algorithm/count_if.hpp>)
#include <range/v3/algorithm/count_if.hpp>
#define XER_AI_HAS_RANGE_V3_HEADER 1
#else
#define XER_AI_HAS_RANGE_V3_HEADER 0
#endif

#if __has_include(<zlib.h>)
#include <zlib.h>
#define XER_AI_HAS_ZLIB_HEADER 1
#else
#define XER_AI_HAS_ZLIB_HEADER 0
#endif

#if __has_include(<uv.h>)
#include <uv.h>
#define XER_AI_HAS_LIBUV_HEADER 1
#else
#define XER_AI_HAS_LIBUV_HEADER 0
#endif

#if __has_include(<unicode/unistr.h>)
#include <unicode/unistr.h>
#define XER_AI_HAS_ICU_HEADER 1
#else
#define XER_AI_HAS_ICU_HEADER 0
#endif

#if ENGINE_HAS_CUDA_BRIDGE && __has_include(<cuda_runtime_api.h>)
#define XER_AI_HAS_CUDA_HEADER 1
#else
#define XER_AI_HAS_CUDA_HEADER 0
#endif

#if ENGINE_HAS_CUDNN_BRIDGE && __has_include(<cudnn.h>)
#define XER_AI_HAS_CUDNN_HEADER 1
#else
#define XER_AI_HAS_CUDNN_HEADER 0
#endif

#if __has_include(<cutlass/cutlass.h>)
#define XER_AI_HAS_CUTLASS_HEADER 1
#else
#define XER_AI_HAS_CUTLASS_HEADER 0
#endif

#if __has_include(<Eigen/Dense>)
#define XER_AI_HAS_EIGEN_HEADER 1
#else
#define XER_AI_HAS_EIGEN_HEADER 0
#endif

#if __has_include(<opencv2/core.hpp>)
#define XER_AI_HAS_OPENCV_HEADER 1
#else
#define XER_AI_HAS_OPENCV_HEADER 0
#endif

#if __has_include(<xnnpack.h>)
#define XER_AI_HAS_XNNPACK_HEADER 1
#else
#define XER_AI_HAS_XNNPACK_HEADER 0
#endif

#if __has_include(<flatbuffers/flatbuffers.h>)
#define XER_AI_HAS_FLATBUFFERS_HEADER 1
#else
#define XER_AI_HAS_FLATBUFFERS_HEADER 0
#endif

#if __has_include(<openvino/openvino.hpp>)
#define XER_AI_HAS_OPENVINO_HEADER 1
#else
#define XER_AI_HAS_OPENVINO_HEADER 0
#endif

#if __has_include(<onnx/onnx_pb.h>)
#define XER_AI_HAS_ONNX_HEADER 1
#else
#define XER_AI_HAS_ONNX_HEADER 0
#endif

#if __has_include(<tensorflow/c/c_api.h>)
#define XER_AI_HAS_TENSORFLOW_HEADER 1
#else
#define XER_AI_HAS_TENSORFLOW_HEADER 0
#endif

#if __has_include(<pthreadpool.h>)
#define XER_AI_HAS_PTHREADPOOL_HEADER 1
#else
#define XER_AI_HAS_PTHREADPOOL_HEADER 0
#endif

#if __has_include(<fp16.h>)
#define XER_AI_HAS_FP16_HEADER 1
#else
#define XER_AI_HAS_FP16_HEADER 0
#endif

namespace XER::ModelsBuilder::Utility {

ExternalLibraryAvailability DetectExternalLibraries() {
    ExternalLibraryAvailability availability;
    availability.has_range_v3 = XER_AI_HAS_RANGE_V3_HEADER == 1;
    availability.has_absl = XER_AI_HAS_ABSL_HEADER == 1;
    availability.has_zlib = XER_AI_HAS_ZLIB_HEADER == 1;
    availability.has_icu = XER_AI_HAS_ICU_HEADER == 1;
    availability.has_libuv = XER_AI_HAS_LIBUV_HEADER == 1;
    availability.has_cuda = XER_AI_HAS_CUDA_HEADER == 1;
    availability.has_cudnn = XER_AI_HAS_CUDNN_HEADER == 1;
    availability.has_cutlass = XER_AI_HAS_CUTLASS_HEADER == 1;
    availability.has_eigen = XER_AI_HAS_EIGEN_HEADER == 1;
    availability.has_opencv = XER_AI_HAS_OPENCV_HEADER == 1;
    availability.has_xnnpack = XER_AI_HAS_XNNPACK_HEADER == 1;
    availability.has_flatbuffers = XER_AI_HAS_FLATBUFFERS_HEADER == 1;
    availability.has_openvino = XER_AI_HAS_OPENVINO_HEADER == 1;
    availability.has_onnx = XER_AI_HAS_ONNX_HEADER == 1;
    availability.has_tensorflow = XER_AI_HAS_TENSORFLOW_HEADER == 1;
    availability.has_pthreadpool = XER_AI_HAS_PTHREADPOOL_HEADER == 1;
    availability.has_fp16 = XER_AI_HAS_FP16_HEADER == 1;
    return availability;
}

std::string BuildRuntimeBanner() {
    const ExternalLibraryAvailability libs = DetectExternalLibraries();

    const std::array<bool, 17> values = {
        libs.has_range_v3,
        libs.has_absl,
        libs.has_zlib,
        libs.has_icu,
        libs.has_libuv,
        libs.has_cuda,
        libs.has_cudnn,
        libs.has_cutlass,
        libs.has_eigen,
        libs.has_opencv,
        libs.has_xnnpack,
        libs.has_flatbuffers,
        libs.has_openvino,
        libs.has_onnx,
        libs.has_tensorflow,
        libs.has_pthreadpool,
        libs.has_fp16,
    };

    size_t enabled_count = 0;
#if XER_AI_HAS_RANGE_V3_HEADER
    enabled_count = static_cast<size_t>(ranges::count_if(values, [](bool value) { return value; }));
#else
    enabled_count = static_cast<size_t>(std::count(values.begin(), values.end(), true));
#endif

#if XER_AI_HAS_ABSL_HEADER
    return absl::StrCat(
        "AI runtime features: ", enabled_count, "/", values.size(),
        " libs enabled (platform=",
        (XER_PLATFORM_WINDOWS ? "windows" : (XER_PLATFORM_MACOS ? "macos" : "posix")),
        ")");
#else
    return "AI runtime features: " + std::to_string(enabled_count) + "/" +
           std::to_string(values.size()) +
           " libs enabled (platform=" +
           std::string(XER_PLATFORM_WINDOWS ? "windows" : (XER_PLATFORM_MACOS ? "macos" : "posix")) +
           ")";
#endif
}

std::string CompressStringFast(std::string_view text) {
#if XER_AI_HAS_ZLIB_HEADER
    if (text.empty()) {
        return {};
    }

    const uLong source_size = static_cast<uLong>(text.size());
    const uLongf bound = compressBound(source_size);
    std::string output(bound, '\0');
    uLongf compressed_size = bound;

    const int result = compress2(
        reinterpret_cast<Bytef*>(output.data()),
        &compressed_size,
        reinterpret_cast<const Bytef*>(text.data()),
        source_size,
        Z_BEST_SPEED);

    if (result != Z_OK) {
        return std::string(text);
    }

    output.resize(compressed_size);
    return output;
#else
    return std::string(text);
#endif
}

size_t SuggestedInferenceThreadCount() {
#if XER_AI_HAS_LIBUV_HEADER
    const unsigned int uv_threads = uv_available_parallelism();
    if (uv_threads > 0U) {
        return static_cast<size_t>(uv_threads);
    }
#endif

    const unsigned int hw_threads = std::thread::hardware_concurrency();
    return hw_threads > 0U ? static_cast<size_t>(hw_threads) : 4U;
}

} // namespace XER::ModelsBuilder::Utility
