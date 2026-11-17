// See color_pick.h — eyedropper + hex helpers over packed BGRA8.
#include "color_pick.h"

#include <algorithm>
#include <cmath>

namespace XER::VideoEditor::Lab {
namespace {

bool IsBgra(const video::Frame& frame) {
    return frame.IsValid() && frame.Format() == video::PixelFormat::BGRA8;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char HexChar(int nibble) {
    return "0123456789ABCDEF"[nibble & 0xF];
}

}  // namespace

std::array<std::uint8_t, 4> SampleRegionAvg(const video::Frame& frame,
                                            int x, int y, int radius) {
    if (!IsBgra(frame)) return {0, 0, 0, 0};
    const int w = frame.Width();
    const int h = frame.Height();
    const int r = std::max(0, radius);
    // Reject windows that never intersect the frame before clamping —
    // a far-off-center must not silently sample the edge pixel.
    if (x + r < 0 || x - r >= w || y + r < 0 || y - r >= h) {
        return {0, 0, 0, 0};
    }
    const int x0 = std::clamp(x - r, 0, w - 1);
    const int x1 = std::clamp(x + r, 0, w - 1);
    const int y0 = std::clamp(y - r, 0, h - 1);
    const int y1 = std::clamp(y + r, 0, h - 1);

    std::uint64_t sum[4] = {0, 0, 0, 0};
    const std::uint8_t* px = frame.Data();
    for (int yy = y0; yy <= y1; ++yy) {
        const std::uint8_t* row =
            px + static_cast<std::size_t>(yy) * w * 4;
        for (int xx = x0; xx <= x1; ++xx) {
            for (int c = 0; c < 4; ++c) sum[c] += row[4 * xx + c];
        }
    }
    const std::uint64_t n =
        static_cast<std::uint64_t>(x1 - x0 + 1) * (y1 - y0 + 1);
    std::array<std::uint8_t, 4> out{};
    for (int c = 0; c < 4; ++c) {
        out[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
            (sum[c] + n / 2) / n);
    }
    return out;
}

float ColorDistance(const std::array<std::uint8_t, 4>& a,
                    const std::array<std::uint8_t, 4>& b) {
    float sum = 0.0f;
    for (int c = 0; c < 4; ++c) {
        const float d = static_cast<float>(
            static_cast<int>(a[static_cast<std::size_t>(c)]) -
            static_cast<int>(b[static_cast<std::size_t>(c)]));
        sum += d * d;
    }
    return std::sqrt(sum);
}

std::array<std::uint8_t, 4> HexToBgra(const std::string& text) {
    std::array<std::uint8_t, 4> out{0, 0, 0, 255};   // opaque black
    std::size_t start = 0;
    if (!text.empty() && text.front() == '#') start = 1;
    const std::size_t digits = text.size() - start;
    if (digits != 6 && digits != 8) return out;
    int nibbles[8];
    for (std::size_t i = 0; i < digits; ++i) {
        const int v = HexDigit(text[start + i]);
        if (v < 0) return out;
        nibbles[i] = v;
    }
    const auto byte = [&nibbles](std::size_t i) {
        return static_cast<std::uint8_t>(nibbles[i] * 16 + nibbles[i + 1]);
    };
    out[2] = byte(0);   // R
    out[1] = byte(2);   // G
    out[0] = byte(4);   // B
    if (digits == 8) {
        out[3] = byte(6);   // A
    }
    return out;
}

std::string BgraToHex(const std::array<std::uint8_t, 4>& bgra,
                      bool include_alpha) {
    // String order is RRGGBB[AA]; the array is {b,g,r,a}.
    const int count = include_alpha ? 8 : 6;
    std::string out;
    out.reserve(static_cast<std::size_t>(count) + 1);
    out.push_back('#');
    const std::uint8_t channels[4] = {bgra[2], bgra[1], bgra[0], bgra[3]};
    const int channel_count = include_alpha ? 4 : 3;
    for (int c = 0; c < channel_count; ++c) {
        out.push_back(HexChar(channels[c] >> 4));
        out.push_back(HexChar(channels[c] & 0xF));
    }
    return out;
}

}  // namespace XER::VideoEditor::Lab
