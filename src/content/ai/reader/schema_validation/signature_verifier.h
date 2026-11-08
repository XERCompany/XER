#pragma once

#include <cstdint>
#include <string>

namespace XER::ModelsBuilder::Reader::Schema {

/// @brief Verifies cryptographic signatures on model files (planned)
///
/// Uses SHA-256 hash verification to ensure model integrity and provenance.
class SignatureVerifier {
 public:
  /// Verify SHA-256 of file data matches expected hex string
  /// @param data        Raw file bytes
  /// @param size        Byte count
  /// @param expected_hex Expected SHA-256 hex string (64 chars)
  static bool VerifySha256(const uint8_t* data, size_t size,
                            const std::string& expected_hex);

  /// Compute SHA-256 hex string (lowercase)
  static std::string ComputeSha256(const uint8_t* data, size_t size);
};

}  // namespace XER::ModelsBuilder::Reader::Schema
