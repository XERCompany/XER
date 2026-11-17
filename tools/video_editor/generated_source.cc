// generated_source.cc — see generated_source.h for the contract.
// Every generator writes packed BGRA8 straight alpha. All three kinds
// are time-invariant today; clip_local_t is accepted for future
// animated sources (e.g. a rolling title).
#include "generated_source.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "bitmap_font.h"

namespace XER::VideoEditor {
namespace {

// Hard ceiling on either generated-frame dimension — a text clip fed
// a novel would otherwise allocate gigabytes.
constexpr int kMaxGeneratedDim = 4096;

// Text styling defaults: the ClipTextStyle face default is opaque
// white ({255,255,255,255}, declared in the header) over a two-tap
// soft drop shadow (a near full-strength tap plus a fainter far tap —
// reads as a soft shadow without needing a blur pass). Offsets scale
// with the glyph so the look holds at every canvas size. An explicit
// outline= style replaces the shadow entirely.
constexpr std::array<std::uint8_t, 4> kShadowNear{0, 0, 0, 120};
constexpr std::array<std::uint8_t, 4> kShadowFar{0, 0, 0, 45};

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

std::string LowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Cell scale a Text clip renders at: style.size_px is a nominal glyph
// height (the bitmap glyph is kGlyphHeight cells tall) rounded to an
// integer scale; 0 falls back to the canvas-proportional default.
int ScaleForTextStyle(const ClipTextStyle& style, int canvas_h) {
    if (style.size_px <= 0) {
        return TextClipGlyphScale(canvas_h);
    }
    return std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(style.size_px) /
            BitmapFont::kGlyphHeight)),
        1, 64);
}

// Splits `utf8` into raw per-line substrings using the same break
// rules as BitmapFont's decoder ('\n'; '\r' also breaks and a '\r\n'
// pair collapses to one break), so alignment offsets line up exactly
// with what DrawText would draw.
std::vector<std::string> SplitTextLines(const std::string& utf8) {
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const char c = utf8[i];
        if (c == '\n') {
            lines.emplace_back();
        } else if (c == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') {
                ++i;   // swallow the LF of a CRLF pair
            }
            lines.emplace_back();
        } else {
            lines.back().push_back(c);
        }
    }
    return lines;
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

bool GenerateText(const Clip& clip, int canvas_h, video::Frame* out) {
    const ClipTextStyle style = ParseClipTextStyle(clip.source.param);
    if (style.text.empty()) {
        // Valid but invisible — a 1x1 transparent frame contributes
        // nothing through the warp/blend stages.
        if (!EnsureFrameSize(out, 1, 1)) {
            return false;
        }
        std::memset(out->Data(), 0, out->DataSize());
        return true;
    }
    const int scale = ScaleForTextStyle(style, canvas_h);
    // Per-line metrics: alignment needs each line's own width so it
    // can be shifted inside the block (the widest line).
    const std::vector<std::string> lines = SplitTextLines(style.text);
    std::vector<int> line_w(lines.size(), 0);
    int block_w = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        int line_h = 0;
        if (!BitmapFont::MeasureText(lines[i], scale, &line_w[i],
                                     &line_h)) {
            return false;   // unreachable (scale >= 1) — belt & braces
        }
        block_w = std::max(block_w, line_w[i]);
    }
    // Same trim as MeasureText: the last line's leading is dropped.
    const int text_h =
        (static_cast<int>(lines.size()) * BitmapFont::kLineAdvance - 2) *
        scale;
    // An outline REPLACES the drop shadow: `edge` is the stroke radius
    // in px and pads every side; `shadow` is the two-tap shadow's
    // +x/+y reach otherwise (the classic look, kept for edge == 0).
    const int edge =
        style.outline_px > 0.0f
            ? std::max(1, static_cast<int>(std::lround(
                              static_cast<double>(style.outline_px))))
            : 0;
    const int shadow = edge == 0 ? 2 * scale : 0;
    const int w =
        std::clamp(block_w + 2 * edge + shadow, 1, kMaxGeneratedDim);
    const int h =
        std::clamp(text_h + 2 * edge + shadow, 1, kMaxGeneratedDim);
    if (!EnsureFrameSize(out, w, h)) {
        return false;
    }
    std::memset(out->Data(), 0, out->DataSize());

    // Draws every line of one pass at offset (dx, dy). Per-line x is
    // the alignment shift: Left -> 0, Center -> half the slack inside
    // the block, Right -> all of it. y walks the fixed line advance.
    auto draw_pass = [&](int dx, int dy,
                         const std::array<std::uint8_t, 4>& bgra) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            int xoff = 0;
            if (style.align == ClipTextStyle::Align::Center) {
                xoff = (block_w - line_w[i]) / 2;
            } else if (style.align == ClipTextStyle::Align::Right) {
                xoff = block_w - line_w[i];
            }
            BitmapFont::DrawText(
                out, lines[i], edge + xoff + dx,
                edge + dy + static_cast<int>(i) *
                                BitmapFont::kLineAdvance * scale,
                scale, bgra);
        }
    };
    if (edge > 0) {
        // 8-direction offset draws fake a stroke around the glyphs.
        static const int kDirs[8][2] = {
            {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
            {1, 0},   {-1, 1}, {0, 1},  {1, 1},
        };
        for (const auto& dir : kDirs) {
            draw_pass(dir[0] * edge, dir[1] * edge, style.outline_color);
        }
    } else {
        // Far tap, near tap — the soft drop shadow.
        draw_pass(2 * scale, 2 * scale, kShadowFar);
        draw_pass(scale, scale, kShadowNear);
    }
    draw_pass(0, 0, style.color);
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

ClipTextStyle ParseClipTextStyle(const std::string& param) {
    ClipTextStyle style;
    const std::size_t bar = param.find('|');
    if (bar == std::string::npos) {
        style.text = param;
        return style;
    }
    style.text = param.substr(0, bar);
    // A space before '|' is separator padding, not title text — the
    // space glyph draws nothing but still pads the measured width.
    while (!style.text.empty() &&
           (style.text.back() == ' ' || style.text.back() == '\t')) {
        style.text.pop_back();
    }
    // Whitespace-separated key=value tokens; unknown keys and
    // malformed values are ignored (lenient — a style typo must never
    // kill the title).
    const std::string spec = param.substr(bar + 1);
    std::size_t i = 0;
    while (i < spec.size()) {
        while (i < spec.size() &&
               std::isspace(static_cast<unsigned char>(spec[i]))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < spec.size() &&
               !std::isspace(static_cast<unsigned char>(spec[i]))) {
            ++i;
        }
        const std::string token = spec.substr(start, i - start);
        const std::size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = LowerAscii(token.substr(0, eq));
        const std::string value = token.substr(eq + 1);
        if (key == "size") {
            const long v = std::strtol(value.c_str(), nullptr, 10);
            if (v > 0) {
                style.size_px =
                    static_cast<int>(std::min<long>(v, 4096));
            }
        } else if (key == "color") {
            style.color = ParseMatteColor(value);
        } else if (key == "outline") {
            style.outline_px = std::max(
                0.0f, std::strtof(value.c_str(), nullptr));
        } else if (key == "outline_color") {
            style.outline_color = ParseMatteColor(value);
        } else if (key == "align") {
            const std::string a = LowerAscii(value);
            if (a == "center") {
                style.align = ClipTextStyle::Align::Center;
            } else if (a == "right") {
                style.align = ClipTextStyle::Align::Right;
            } else if (a == "left") {
                style.align = ClipTextStyle::Align::Left;
            }
        }
    }
    return style;
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
            return GenerateText(clip, canvas_h, out);
        case SourceKind::Media:
        default:
            return false;   // Media decodes; unknown kinds are errors
    }
}

}  // namespace XER::VideoEditor
