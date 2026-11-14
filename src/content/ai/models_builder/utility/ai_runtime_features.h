#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace XER::ModelsBuilder::Utility {

struct ExternalLibraryAvailability {
    bool has_range_v3 = false;
    bool has_absl = false;
    bool has_zlib = false;
    bool has_icu = false;
    bool has_libuv = false;
    bool has_cuda = false;
    bool has_cudnn = false;
    bool has_cutlass = false;
    bool has_eigen = false;
    bool has_opencv = false;
    bool has_xnnpack = false;
    bool has_flatbuffers = false;
    bool has_openvino = false;
    bool has_onnx = false;
    bool has_tensorflow = false;
    bool has_pthreadpool = false;
    bool has_fp16 = false;
    bool has_ggml = false;
    bool has_mkl = false;
    bool has_openblas = false;
    bool has_dnnl = false;
    bool has_fbgemm = false;
    bool has_cudnn_frontend = false;
    bool has_protobuf = false;
    bool has_tesseract = false;
    bool has_yaml_cpp = false;
    bool has_blaze = false;
    bool has_asmjit = false;
    bool has_glslang = false;
    bool has_shaderc = false;
    bool has_spirv_tools = false;
    bool has_vulkan = false;
    bool has_vulkan_loader = false;
    bool has_vulkan_util = false;
    bool has_vma = false;
    bool has_level_zero = false;
    bool has_nlohmann_json = false;
    bool has_rapidjson = false;
    bool has_tinyxml2 = false;
    bool has_date = false;
    bool has_json_schema_validator = false;
    bool has_lz4 = false;
    bool has_snappy = false;
    bool has_brotli = false;
    bool has_tinyexr = false;
    bool has_leptonica = false;
    bool has_pffft = false;
    bool has_curl = false;
    bool has_re2 = false;
    bool has_sqlite3 = false;
    bool has_kineto = false;
};

ExternalLibraryAvailability DetectExternalLibraries();
std::string BuildRuntimeBanner();
std::string CompressStringFast(std::string_view text);
std::string DecompressStringFast(std::string_view compressed);
size_t SuggestedInferenceThreadCount();

} // namespace XER::ModelsBuilder::Utility
