/**
 * Decryptor — CDM-facing decryption interface (design doc: EME path
 * wraps ClearKey/Widevine-style modules; the demuxer emits encrypted
 * units, the decryptor returns clear DecoderBuffer-equivalent bytes).
 * Stubbed API surface — real CDM hosting lands with the IPC layer.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Media {

enum class CdmStatus : std::uint8_t {
    kOk,
    kNoKey,
    kNeedMoreData,
    kError,
};

struct DecryptConfig {
    std::vector<std::uint8_t> key_id;
    std::vector<std::uint8_t> iv;
    bool requires_subsamples = false;
};

class Decryptor {
public:
    virtual ~Decryptor() = default;

    // Decrypts |encrypted| into |clear_out|. kNeedMoreData when the unit
    // is truncated; kNoKey when the key for |config| is absent.
    virtual CdmStatus Decrypt(const DecryptConfig& config,
                              const std::vector<std::uint8_t>& encrypted,
                              std::vector<std::uint8_t>* clear_out,
                              std::string* error_out) = 0;
    virtual bool IsKeyAvailable(
        const std::vector<std::uint8_t>& key_id) const = 0;
};

}  // namespace XER::Media
