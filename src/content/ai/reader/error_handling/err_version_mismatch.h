#pragma once

#include <stdexcept>
#include <string>

namespace XER::ModelsBuilder::Reader {

/// Thrown when the model file's format version is not compatible with the
/// reader (e.g. ONNX opset 20+ when only opsets ≤17 are supported).
class VersionMismatchError : public std::runtime_error {
public:
    explicit VersionMismatchError(const std::string& msg)
        : std::runtime_error("[VersionMismatch] " + msg) {}

    VersionMismatchError(int found, int max_supported)
        : std::runtime_error("[VersionMismatch] version " + std::to_string(found)
                             + " exceeds max supported " + std::to_string(max_supported)) {}
};

}  // namespace XER::ModelsBuilder::Reader
