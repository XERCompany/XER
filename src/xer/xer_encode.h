#pragma once

#include "config.h"
#include "xer/encode/sxer84321.h"
#include "xer/xer_compiler.h"
#include "xer/xer_buffer.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Xer {

struct XerProtectionOptions {
    std::string encryption_key;

    bool HasEncryption() const { return !encryption_key.empty(); }
};

XerProtectionOptions ResolveProtectionOptionsFromEnvironment(std::string* error_out = nullptr);

struct XerEncodedBlock {
    std::string algorithm;
    XerBuffer bin_payload;
    std::string bak_manifest;
    std::uint64_t nonce = 0;
    std::uint32_t source_crc = 0;
    std::uint32_t payload_crc = 0;
    bool encrypted = false;
    std::string encryption = "none";
};

class XerEncode {
public:
    XerEncodedBlock Encode(
        const XerBuffer& buffer,
        std::string_view logical_name = "script",
        XerProtectionOptions options = {}) const;
    XerEncodedBlock Compile(
        const std::filesystem::path& source_path,
        XerProtectionOptions options = {},
        std::string* error_out = nullptr) const;
    XerBuffer Decode(
        const XerBuffer& payload,
        XerProtectionOptions options = {},
        std::string* error_out = nullptr) const;
    bool InspectFile(
        const std::filesystem::path& artifact_path,
        std::string* report_out,
        std::string* error_out = nullptr) const;
    bool WriteArtifacts(
        const std::filesystem::path& source_path,
        const XerEncodedBlock& block,
        const std::filesystem::path& output_dir = {},
        std::string* error_out = nullptr) const;
};

}  // namespace Xer