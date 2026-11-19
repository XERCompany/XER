/**
 * SpanReader — bounds-checked little-endian reader over a byte span
 * (design doc mentions XER::Helper::SpanReader for parsing paths).
 * Zero-allocation; every read bounds-checks and fails cleanly.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace XER::Helper {

class SpanReader {
public:
    SpanReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::size_t remaining() const { return size_ - pos_; }
    std::size_t position() const { return pos_; }
    bool empty() const { return pos_ >= size_; }

    bool ReadU8(std::uint8_t* out) {
        if (remaining() < 1) return false;
        *out = data_[pos_++];
        return true;
    }
    bool ReadU16(std::uint16_t* out) {
        if (remaining() < 2) return false;
        *out = static_cast<std::uint16_t>(data_[pos_]) |
               (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return true;
    }
    bool ReadU32(std::uint32_t* out) {
        if (remaining() < 4) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 4;
        *out = v;
        return true;
    }
    bool ReadU64(std::uint64_t* out) {
        if (remaining() < 8) return false;
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 8;
        *out = v;
        return true;
    }
    // Big-endian variants for network/container formats (EBML/MP4).
    bool ReadU32BE(std::uint32_t* out) {
        if (remaining() < 4) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v = (v << 8) | data_[pos_ + i];
        pos_ += 4;
        *out = v;
        return true;
    }
    bool ReadBytes(std::uint8_t* out, std::size_t n) {
        if (remaining() < n) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }
    bool ReadString(std::size_t n, std::string* out) {
        if (remaining() < n) return false;
        out->assign(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return true;
    }
    bool Skip(std::size_t n) {
        if (remaining() < n) return false;
        pos_ += n;
        return true;
    }
    const std::uint8_t* cursor() const { return data_ + pos_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

}  // namespace XER::Helper
