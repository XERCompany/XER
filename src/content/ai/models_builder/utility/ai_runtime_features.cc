#include "ai_runtime_features.h"

#include "../../ml/utils/ggml_runtime.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
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

#if __has_include(<zstd.h>)
#include <zstd.h>
#define XER_AI_HAS_ZSTD_HEADER 1
#else
#define XER_AI_HAS_ZSTD_HEADER 0
#endif

// zstd frame magic (little-endian 0xFD2FB528) used to distinguish zstd
// payloads from legacy zlib blobs in DecompressStringFast.
#if XER_AI_HAS_ZSTD_HEADER
constexpr uint32_t kZstdFrameMagic = 0xFD2FB528U;
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

    // GPU compute / shader toolchain.
#ifdef XER_AI_HAS_ASMJIT
    availability.has_asmjit = true;
#endif
#ifdef XER_AI_HAS_GLSLANG
    availability.has_glslang = true;
#endif
#ifdef XER_AI_HAS_SHADERC
    availability.has_shaderc = true;
#endif
#ifdef XER_AI_HAS_SPIRV_TOOLS
    availability.has_spirv_tools = true;
#endif
#ifdef XER_AI_HAS_VULKAN
    availability.has_vulkan = true;
#endif
#ifdef XER_AI_HAS_VULKAN_LOADER
    availability.has_vulkan_loader = true;
#endif
#ifdef XER_AI_HAS_VULKAN_UTIL
    availability.has_vulkan_util = true;
#endif
#ifdef XER_AI_HAS_VMA
    availability.has_vma = true;
#endif
#ifdef XER_AI_HAS_LEVEL_ZERO
    availability.has_level_zero = true;
#endif

    // Serialization / config formats.
#ifdef XER_AI_HAS_NLOHMANN_JSON
    availability.has_nlohmann_json = true;
#endif
#ifdef XER_AI_HAS_RAPIDJSON
    availability.has_rapidjson = true;
#endif
#ifdef XER_AI_HAS_TINYXML2
    availability.has_tinyxml2 = true;
#endif
#ifdef XER_AI_HAS_DATE
    availability.has_date = true;
#endif
#ifdef XER_AI_HAS_JSON_SCHEMA_VALIDATOR
    availability.has_json_schema_validator = true;
#endif

    // Compression codecs.
#ifdef XER_AI_HAS_LZ4
    availability.has_lz4 = true;
#endif
#ifdef XER_AI_HAS_SNAPPY
    availability.has_snappy = true;
#endif
#ifdef XER_AI_HAS_BROTLI
    availability.has_brotli = true;
#endif

    // Imaging / signal processing.
#ifdef XER_AI_HAS_TINYEXR
    availability.has_tinyexr = true;
#endif
#ifdef XER_AI_HAS_LEPTONICA
    availability.has_leptonica = true;
#endif
#ifdef XER_AI_HAS_PFFFT
    availability.has_pffft = true;
#endif

    // Infrastructure.
#ifdef XER_AI_HAS_CURL
    availability.has_curl = true;
#endif
#ifdef XER_AI_HAS_RE2
    availability.has_re2 = true;
#endif
#ifdef XER_AI_HAS_SQLITE3
    availability.has_sqlite3 = true;
#endif
#ifdef XER_AI_HAS_KINETO
    availability.has_kineto = true;
#endif
    return availability;
}

std::string BuildRuntimeBanner() {
    const ExternalLibraryAvailability libs = DetectExternalLibraries();

    const std::array<bool, 51> values = {
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
        libs.has_asmjit,
        libs.has_glslang,
        libs.has_shaderc,
        libs.has_spirv_tools,
        libs.has_vulkan,
        libs.has_vulkan_loader,
        libs.has_vulkan_util,
        libs.has_vma,
        libs.has_level_zero,
        libs.has_nlohmann_json,
        libs.has_rapidjson,
        libs.has_tinyxml2,
        libs.has_date,
        libs.has_json_schema_validator,
        libs.has_lz4,
        libs.has_snappy,
        libs.has_brotli,
        libs.has_tinyexr,
        libs.has_leptonica,
        libs.has_pffft,
        libs.has_curl,
        libs.has_re2,
        libs.has_sqlite3,
        libs.has_kineto,
    };

    size_t enabled_count = 0;
#if XER_AI_HAS_RANGE_V3_HEADER
    enabled_count = static_cast<size_t>(ranges::count_if(values, [](bool value) { return value; }));
#else
    enabled_count = static_cast<size_t>(std::count(values.begin(), values.end(), true));
#endif

#if XER_AI_HAS_ABSL_HEADER
    std::string banner = absl::StrCat(
        "AI runtime features: ", enabled_count, "/", values.size(),
        " libs enabled (platform=",
        (XER_PLATFORM_WINDOWS ? "windows" : (XER_PLATFORM_MACOS ? "macos" : "posix")),
        ")");
#else
    std::string banner = "AI runtime features: " + std::to_string(enabled_count) + "/" +
           std::to_string(values.size()) +
           " libs enabled (platform=" +
           std::string(XER_PLATFORM_WINDOWS ? "windows" : (XER_PLATFORM_MACOS ? "macos" : "posix")) +
           ")";
#endif

#ifdef XER_AI_HAS_GGML
    banner += " | " + ::XER::ML::Utils::GgmlRuntimeSummary();
#endif
    return banner;
}

std::string CompressStringFast(std::string_view text) {
#if XER_AI_HAS_ZSTD_HEADER
    // zstd preferred: better ratio than zlib at comparable speed.
    if (!text.empty()) {
        const size_t bound = ZSTD_compressBound(text.size());
        std::string output(bound, '\0');
        const size_t n = ZSTD_compress(output.data(), bound,
                                       text.data(), text.size(), 3);
        if (!ZSTD_isError(n)) {
            output.resize(n);
            return output;
        }
    }
    return {};
#elif XER_AI_HAS_ZLIB_HEADER
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

std::string DecompressStringFast(std::string_view compressed) {
#if XER_AI_HAS_ZSTD_HEADER
    if (compressed.size() >= sizeof(uint32_t)) {
        uint32_t magic = 0;
        std::memcpy(&magic, compressed.data(), sizeof(magic));
        if (magic == kZstdFrameMagic) {
            const unsigned long long content_size =
                ZSTD_getFrameContentSize(compressed.data(), compressed.size());
            if (content_size == ZSTD_CONTENTSIZE_ERROR ||
                content_size == ZSTD_CONTENTSIZE_UNKNOWN ||
                content_size > (512ULL << 20)) {
                return {};
            }
            std::string output(static_cast<size_t>(content_size), '\0');
            const size_t n = ZSTD_decompress(output.data(), output.size(),
                                             compressed.data(),
                                             compressed.size());
            if (ZSTD_isError(n)) {
                return {};
            }
            output.resize(n);
            return output;
        }
    }
#endif
#if XER_AI_HAS_ZLIB_HEADER
    if (compressed.empty()) {
        return {};
    }

    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return {};
    }

    const Bytef* input_pos = reinterpret_cast<const Bytef*>(compressed.data());
    size_t input_left = compressed.size();
    std::string output;
    output.resize(64U * 1024U);
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    int status = Z_OK;

    while (status == Z_OK) {
        if (stream.avail_in == 0U && input_left > 0U) {
            const uInt chunk = static_cast<uInt>(std::min<size_t>(input_left, UINT_MAX));
            stream.next_in = const_cast<Bytef*>(input_pos);
            stream.avail_in = chunk;
            input_pos += chunk;
            input_left -= chunk;
        }
        if (stream.avail_out == 0U) {
            if (output.size() >= (std::numeric_limits<uInt>::max() / 2U)) {
                status = Z_MEM_ERROR;
                break;
            }
            output.resize(output.size() * 2U);
            stream.next_out = reinterpret_cast<Bytef*>(output.data()) + stream.total_out;
            stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);
        }
        status = inflate(&stream, Z_NO_FLUSH);
    }

    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return {};
    }
    output.resize(stream.total_out);
    return output;
#else
    return std::string(compressed);
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
