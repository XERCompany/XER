#include "signature_verifier.h"

#include <iomanip>
#include <sstream>

// Prefer OpenSSL SHA-256 (linked as OpenSSL::Crypto in main.cmake).
#if __has_include(<openssl/sha.h>)
#include <openssl/sha.h>
#define XER_HAS_OPENSSL_SHA 1
#else
// Fallback: zlib CRC-32 (8-hex-digit "hash").
#include <zlib.h>
#define XER_HAS_OPENSSL_SHA 0
#endif

namespace XER::ModelsBuilder::Reader::Schema {

std::string SignatureVerifier::ComputeSha256(const uint8_t* data, size_t size) {
#if XER_HAS_OPENSSL_SHA
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(data, size, digest);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : digest)
        oss << std::setw(2) << static_cast<unsigned>(b);
    return oss.str();
#else
    // CRC-32 fallback — produces an 8-character hex string.
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(size));
    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << crc;
    return oss.str();
#endif
}

bool SignatureVerifier::VerifySha256(const uint8_t* data, size_t size,
                                      const std::string& expected_hex) {
    return ComputeSha256(data, size) == expected_hex;
}

}  // namespace XER::ModelsBuilder::Reader::Schema
