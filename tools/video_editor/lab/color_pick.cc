// See color_pick.h — eyedropper + hex helpers over packed BGRA8.
#include "color_pick.h"

#include <algorithm>
#include <cmath>

#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Lab {
namespace {

bool IsBgra(const video::Frame& frame) {
    return frame.IsValid() && frame.Format() == video::PixelFormat::BGRA8;
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
    // Same grammar the bridge parser accepts: optional '#', 6 or 8
    // case-insensitive RRGGBB[AA] digits; malformed -> opaque black.
    std::uint32_t rgba = 0;
    if (!XER::Bridge::Skia::ParseColorHex(text, &rgba)) {
        return out;
    }
    out[2] = static_cast<std::uint8_t>((rgba >> 24) & 0xFF);   // R
    out[1] = static_cast<std::uint8_t>((rgba >> 16) & 0xFF);   // G
    out[0] = static_cast<std::uint8_t>((rgba >> 8) & 0xFF);    // B
    out[3] = static_cast<std::uint8_t>(rgba & 0xFF);           // A
    return out;
}

std::string BgraToHex(const std::array<std::uint8_t, 4>& bgra,
                      bool include_alpha) {
    // ColorToHex emits the same canonical "#RRGGBB[AA]" uppercase form.
    const std::uint32_t rgba = XER::Bridge::Skia::MakeColorRGBA(
        bgra[2], bgra[1], bgra[0], bgra[3]);
    return XER::Bridge::Skia::ColorToHex(rgba, include_alpha);
}

}  // namespace XER::VideoEditor::Lab
