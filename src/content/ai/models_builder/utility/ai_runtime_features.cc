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

    // Backends wired through CMake (XER_AI_HAS_* compile definitions set by
    // src/content/ai/CMakeLists.txt when the vcpkg port is present).
#ifdef XER_AI_HAS_GGML
    availability.has_ggml = true;
#endif
#ifdef XER_AI_HAS_MKL
    availability.has_mkl = true;
#endif
#ifdef XER_AI_HAS_OPENBLAS
    availability.has_openblas = true;
#endif
#ifdef XER_AI_HAS_DNNL
    availability.has_dnnl = true;
#endif
#ifdef XER_AI_HAS_FBGEMM
    availability.has_fbgemm = true;
#endif
#ifdef XER_AI_HAS_CUDNN_FRONTEND
    availability.has_cudnn_frontend = true;
#endif
#ifdef XER_AI_HAS_PROTOBUF
    availability.has_protobuf = true;
#endif
#ifdef XER_AI_HAS_TESSERACT
    availability.has_tesseract = true;
#endif
#ifdef XER_AI_HAS_YAML_CPP
    availability.has_yaml_cpp = true;
#endif
#ifdef XER_AI_HAS_BLAZE
    availability.has_blaze = true;
#endif
#ifdef XER_AI_HAS_CUDA_LIB
    availability.has_cuda = true;
#endif
#ifdef XER_AI_HAS_CUDNN_LIB
    availability.has_cudnn = true;
#endif
    return availability;
}

std::string BuildRuntimeBanner() {
    const ExternalLibraryAvailability libs = DetectExternalLibraries();

    const std::array<bool, 27> values = {
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
        libs.has_ggml,
        libs.has_mkl,
        libs.has_openblas,
        libs.has_dnnl,
        libs.has_fbgemm,
        libs.has_cudnn_frontend,
        libs.has_protobuf,
        libs.has_tesseract,
        libs.has_yaml_cpp,
        libs.has_blaze,
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
