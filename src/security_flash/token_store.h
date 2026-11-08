#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "security_flash/security_flash.h"

namespace XER::SecurityFlash::Detail {

// Serializes a token payload into the complete on-disk file image
// ([header][AES-256-GCM ciphertext]). payload.volume_serial != 0 marks
// the token as bound to that volume.
bool BuildTokenFile(const TokenPayload& payload,
		    const std::string& passphrase,
		    std::vector<std::uint8_t>* file_bytes_out,
		    std::string* error_out);

// Parses and decrypts a token file image. Does NOT verify volume
// binding — the caller compares payload.volume_serial with the
// target volume via VolumeSerialForPath.
bool ParseTokenFile(const std::vector<std::uint8_t>& file_bytes,
		    const std::string& passphrase,
		    TokenPayload* payload_out,
		    std::string* error_out);

}  // namespace XER::SecurityFlash::Detail
