#pragma once

#include <stdexcept>
#include <string>

namespace XER::ModelsBuilder::Reader {

/// Thrown when an operator or layer type present in the model file is not
/// supported by the current XER runtime (e.g. a ONNX op with no converter).
class UnsupportedOpError : public std::runtime_error {
public:
    explicit UnsupportedOpError(const std::string& op_name)
        : std::runtime_error("[UnsupportedOp] '" + op_name + "' is not supported") {}

    UnsupportedOpError(const std::string& op_name, const std::string& context)
        : std::runtime_error("[UnsupportedOp] '" + op_name + "' in " + context) {}
};

}  // namespace XER::ModelsBuilder::Reader
