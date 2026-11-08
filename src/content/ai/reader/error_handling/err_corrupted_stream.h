#pragma once

#include <stdexcept>
#include <string>

namespace XER::ModelsBuilder::Reader {

/// Thrown when a model byte stream is structurally corrupted (truncated magic,
/// invalid header checksum, broken length prefix, etc.)
class CorruptedStreamError : public std::runtime_error {
public:
    explicit CorruptedStreamError(const std::string& msg)
        : std::runtime_error("[CorruptedStream] " + msg) {}

    CorruptedStreamError(const std::string& source, const std::string& detail)
        : std::runtime_error("[CorruptedStream] " + source + ": " + detail) {}
};

}  // namespace XER::ModelsBuilder::Reader
