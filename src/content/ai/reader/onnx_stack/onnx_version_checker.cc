#include "onnx_version_checker.h"

namespace XER::ModelsBuilder::Reader::Onnx {

bool OnnxVersionChecker::IsOpsetSupported(int opset_version) noexcept {
    return opset_version >= kMinSupportedOpset &&
           opset_version <= kMaxSupportedOpset;
}

std::string OnnxVersionChecker::Describe(int opset_version) {
    if (IsOpsetSupported(opset_version))
        return "ONNX opset " + std::to_string(opset_version) + " is supported.";
    return "ONNX opset " + std::to_string(opset_version)
         + " is NOT supported (supported range: "
         + std::to_string(kMinSupportedOpset) + " - "
         + std::to_string(kMaxSupportedOpset) + ").";
}

}  // namespace XER::ModelsBuilder::Reader::Onnx
