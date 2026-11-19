/**
 * SpanWriter — bounds-checked little/big-endian writer into a byte
 * buffer (design doc: XER::Helper::SpanWriter). Appends to a
 * std::vector (grow path) or writes into a fixed span (no-alloc path
 * for RT contexts).
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace XER::Helper {

class SpanWriter {
public:
    // Grow mode: appends to |buf|.
    explicit SpanWriter(std::vector<std::uint8_t>* buf) : buf_(buf) {}
    // Fixed mode: writes into |span|, fails past capacity.
    SpanWriter(std::uint8_t* span, std::size_t capacity)
        : span_(span), capacity_(capacity) {}

    std::size_t size() const { return pos_; }

    bool WriteU8(std::uint8_t v) { return Write(&v, 1); }
    bool WriteU16(std::uint16_t v) {
        const std::uint8_t b[] = {static_cast<std::uint8_t>(v),
                                  static_cast<std::uint8_t>(v >> 8)};
        return Write(b, 2);
    }
    bool WriteU32(std::uint32_t v) {
        std::uint8_t b[4];
        for (int i = 0; i < 4; ++i)
            b[i] = static_cast<std::uint8_t>(v >> (8 * i));
        return Write(b, 4);
    }
    bool WriteU64(std::uint64_t v) {
        std::uint8_t b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = static_cast<std::uint8_t>(v >> (8 * i));
        return Write(b, 8);
    }
    bool WriteU32BE(std::uint32_t v) {
        std::uint8_t b[4];
        for (int i = 0; i < 4; ++i)
            b[i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
        return Write(b, 4);
    }
    bool Write(const std::uint8_t* data, std::size_t n) {
        if (buf_) {
            buf_->insert(buf_->end(), data, data + n);
            pos_ += n;
            return true;
        }
        if (pos_ + n > capacity_) return false;
        std::memcpy(span_ + pos_, data, n);
        pos_ += n;
        return true;
    }

private:
    std::vector<std::uint8_t>* buf_ = nullptr;
    std::uint8_t* span_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t pos_ = 0;
};

}  // namespace XER::Helper
