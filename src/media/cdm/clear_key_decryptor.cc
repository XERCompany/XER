#include "media/cdm/clear_key_decryptor.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

void ClearKeyDecryptor::AddKey(
    const std::vector<std::uint8_t>& key_id,
    const std::vector<std::uint8_t>& key) {
    keys_[key_id] = key;
}

bool ClearKeyDecryptor::IsKeyAvailable(
    const std::vector<std::uint8_t>& key_id) const {
    return keys_.count(key_id) != 0;
}

CdmStatus ClearKeyDecryptor::Decrypt(
    const DecryptConfig& config,
    const std::vector<std::uint8_t>& encrypted,
    std::vector<std::uint8_t>* clear_out, std::string* error_out) {
    const auto it = keys_.find(config.key_id);
    if (it == keys_.end()) return CdmStatus::kNoKey;
    if (config.iv.size() < 8 || it->second.size() != 16) {
        if (error_out) *error_out = "invalid clear-key config";
        return CdmStatus::kError;
    }
    // AES-128-CTR keystream placeholder: XOR with key-derived stream so
    // the encrypted path is exercised end-to-end until real AES lands.
    clear_out->resize(encrypted.size());
    const auto& key = it->second;
    for (std::size_t i = 0; i < encrypted.size(); ++i) {
        const std::uint8_t ks =
            key[i % 16] ^ config.iv[i % config.iv.size()] ^
            static_cast<std::uint8_t>(i >> 8);
        (*clear_out)[i] = encrypted[i] ^ ks;
    }
    return CdmStatus::kOk;
}

}  // namespace XER::Media
