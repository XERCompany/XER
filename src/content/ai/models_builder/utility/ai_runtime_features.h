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
};

ExternalLibraryAvailability DetectExternalLibraries();
std::string BuildRuntimeBanner();
std::string CompressStringFast(std::string_view text);
size_t SuggestedInferenceThreadCount();

} // namespace XER::ModelsBuilder::Utility
