#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Utils {

/// Byte-swapping helpers for cross-endian model file loading.
class RmEndianSwap {
public:
    static uint16_t Swap16(uint16_t v) noexcept;
    static uint32_t Swap32(uint32_t v) noexcept;
    static uint64_t Swap64(uint64_t v) noexcept;
    static float    SwapFloat(float v) noexcept;

    /// Swap each 4-byte word in a buffer in-place
    static void SwapBuffer32(std::vector<uint8_t>& buf) noexcept;

    /// Returns true if the running machine is big-endian
    static bool IsBigEndian() noexcept;
};

}  // namespace XER::ModelsBuilder::Reader::Utils
