#include "rm_endian_swap.h"
#include <cstring>

namespace XER::ModelsBuilder::Reader::Utils {

uint16_t RmEndianSwap::Swap16(uint16_t v) noexcept {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}

uint32_t RmEndianSwap::Swap32(uint32_t v) noexcept {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}

uint64_t RmEndianSwap::Swap64(uint64_t v) noexcept {
    v = ((v & 0x00000000FFFFFFFFull) << 32) | ((v & 0xFFFFFFFF00000000ull) >> 32);
    v = ((v & 0x0000FFFF0000FFFFull) << 16) | ((v & 0xFFFF0000FFFF0000ull) >> 16);
    v = ((v & 0x00FF00FF00FF00FFull) <<  8) | ((v & 0xFF00FF00FF00FF00ull) >>  8);
    return v;
}

float RmEndianSwap::SwapFloat(float v) noexcept {
    uint32_t tmp;
    std::memcpy(&tmp, &v, sizeof(tmp));
    tmp = Swap32(tmp);
    std::memcpy(&v, &tmp, sizeof(v));
    return v;
}

void RmEndianSwap::SwapBuffer32(std::vector<uint8_t>& buf) noexcept {
    const size_t n = buf.size() / 4;
    auto* ptr = reinterpret_cast<uint32_t*>(buf.data());
    for (size_t i = 0; i < n; ++i)
        ptr[i] = Swap32(ptr[i]);
}

bool RmEndianSwap::IsBigEndian() noexcept {
    const uint32_t probe = 0x01020304u;
    uint8_t first;
    std::memcpy(&first, &probe, 1);
    return first == 0x01;
}

}  // namespace XER::ModelsBuilder::Reader::Utils
