// generated_source.cc — see generated_source.h for the contract.
// Every generator writes packed BGRA8 straight alpha. All three kinds
// are time-invariant today; clip_local_t is accepted for future
// animated sources (e.g. a rolling title).
#include "generated_source.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

namespace XER::VideoEditor {
namespace {

// Hard ceiling on either generated-frame dimension — a text clip fed
// a novel would otherwise allocate gigabytes.
constexpr int kMaxGeneratedDim = 4096;

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const int lower = std::tolower(static_cast<unsigned char>(c));
    if (lower >= 'a' && lower <= 'f') {
        return lower - 'a' + 10;
    }
    return -1;
}

// Ensures `out` is a valid w*h BGRA8 frame (reusing it when it
// already matches — matte/bars frames are then just refilled).
bool EnsureFrameSize(video::Frame* out, int w, int h) {
    if (out->Width() != w || out->Height() != h ||
        out->Format() != video::PixelFormat::BGRA8) {
        out->Release();
        if (!out->Allocate(w, h, video::PixelFormat::BGRA8)) {
            return false;
        }
    }
    return true;
}

void FillSolid(video::Frame* frame,
               const std::array<std::uint8_t, 4>& bgra) {
    std::uint8_t* p = frame->Data();
    const int count = frame->Width() * frame->Height();
    for (int i = 0; i < count; ++i) {
        p[i * 4 + 0] = bgra[0];
        p[i * 4 + 1] = bgra[1];
        p[i * 4 + 2] = bgra[2];
        p[i * 4 + 3] = bgra[3];
    }
}

bool GenerateColorMatte(const Clip& clip, int canvas_w, int canvas_h,
                        video::Frame* out) {
    if (!EnsureFrameSize(out, canvas_w, canvas_h)) {
        return false;
    }
    FillSolid(out, ParseMatteColor(clip.source.param));
    return true;
}

bool GenerateColorBars(const Clip& /*clip*/, int canvas_w, int canvas_h,
                       video::Frame* out) {
    if (!EnsureFrameSize(out, canvas_w, canvas_h)) {
        return false;
    }
    const auto& bars = Smpte75Bars();
    std::uint8_t* p = out->Data();
    for (int y = 0; y < canvas_h; ++y) {
        for (int x = 0; x < canvas_w; ++x) {
            // Integer-mapped column: bar i covers [i*w/7, (i+1)*w/7).
            const int bar =
                std::clamp(x * 7 / canvas_w, 0, 6);
            const auto& c = bars[static_cast<std::size_t>(bar)];
            std::uint8_t* d =
                p + (static_cast<std::size_t>(y) * canvas_w + x) * 4u;
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
            d[3] = c[3];
        }
    }
    return true;
}

}  // namespace

std::array<std::uint8_t, 4> ParseMatteColor(const std::string& param) {
    // Default: opaque black.
    std::array<std::uint8_t, 4> out{0, 0, 0, 255};
    std::size_t start = 0;
    if (!param.empty() && param.front() == '#') {
        start = 1;
    }
    const std::size_t digits = param.size() - start;
    if (digits != 6 && digits != 8) {
        return out;
    }
    int nibbles[8];
    for (std::size_t i = 0; i < digits; ++i) {
        const int v = HexDigit(param[start + i]);
        if (v < 0) {
            return out;
        }
        nibbles[i] = v;
    }
    const auto byte = [&](std::size_t i) {
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

const std::array<std::array<std::uint8_t, 4>, 7>& Smpte75Bars() {
    // SMPTE EG 1-1990 75% bars: gray, yellow, cyan, green, magenta,
    // red, blue — each 191 (0xBF) = 0.75*255 on the lit channels.
    static const std::array<std::array<std::uint8_t, 4>, 7> bars{{
        {191, 191, 191, 255},   // gray
        {0, 191, 191, 255},     // yellow
        {191, 191, 0, 255},     // cyan
        {0, 191, 0, 255},       // green
        {191, 0, 191, 255},     // magenta
        {0, 0, 191, 255},       // red
        {191, 0, 0, 255},       // blue
    }};
    return bars;
}

int TextClipGlyphScale(int canvas_h) {
    if (canvas_h <= 0) {
        return 1;
    }
    // Line advance is 9 cells tall: canvas_h/135 puts one text line
    // near 1/15th of the frame height (1080/135 -> 8).
    return std::clamp(canvas_h / 135, 1, 64);
}

bool GenerateSourceFrame(const Clip& clip, double /*clip_local_t*/,
                         int canvas_w, int canvas_h, video::Frame* out) {
    if (out == nullptr || canvas_w <= 0 || canvas_h <= 0) {
        return false;
    }
    if (canvas_w > kMaxGeneratedDim || canvas_h > kMaxGeneratedDim) {
        return false;
    }
    switch (clip.source.kind) {
        case SourceKind::ColorMatte:
            return GenerateColorMatte(clip, canvas_w, canvas_h, out);
        case SourceKind::ColorBars:
            return GenerateColorBars(clip, canvas_w, canvas_h, out);
        case SourceKind::Text:
            // Parse + rasterize lives in text_style.cc (Skia path,
            // BitmapFont fallback) — see text_style.h for the tokens.
            return RenderClipText(
                ParseClipTextStyle(clip.source.param), canvas_h, out);
        case SourceKind::Media:
        default:
            return false;   // Media decodes; unknown kinds are errors
    }
}

}  // namespace XER::VideoEditor
