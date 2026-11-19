/**
 * ClearKeyDecryptor — reference Decryptor implementing the ClearKey
 * system for testing the encrypted pipeline path (AES-128-CTR).
 */
#pragma once

#include <map>

#include "media/cdm/decryptor.h"

namespace XER::Media {

class ClearKeyDecryptor final : public Decryptor {
public:
    // Registers a 16-byte AES-128 key for |key_id|.
    void AddKey(const std::vector<std::uint8_t>& key_id,
                const std::vector<std::uint8_t>& key);

    CdmStatus Decrypt(const DecryptConfig& config,
                      const std::vector<std::uint8_t>& encrypted,
                      std::vector<std::uint8_t>* clear_out,
                      std::string* error_out) override;
    bool IsKeyAvailable(
        const std::vector<std::uint8_t>& key_id) const override;

private:
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> keys_;
};

}  // namespace XER::Media
