// text_style.cc — see text_style.h for the param grammar and the
// ClipTextStyle contract. Three jobs live here: the lenient parser,
// its Serialize inverse, and RenderClipText (Skia when the build links
// it, BitmapFont otherwise).
#include "text_style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bitmap_font.h"
#include "generated_source.h"   // ParseMatteColor / TextClipGlyphScale

#if defined(XER_VE_HAS_SKIA)
#include <core/SkBlurTypes.h>
#include <core/SkCanvas.h>
#include <core/SkColor.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontMgr.h>
#include <core/SkImageInfo.h>
#include <core/SkMaskFilter.h>
#include <core/SkPaint.h>
#include <core/SkPixmap.h>
#include <core/SkRRect.h>
#include <core/SkRect.h>
#include <core/SkSurface.h>
#include <core/SkTypeface.h>
#if defined(_WIN32) && __has_include(<ports/SkTypeface_win.h>)
#include <ports/SkTypeface_win.h>
#define XER_TEXT_STYLE_GDI_MGR 1
#endif
#endif

namespace XER::VideoEditor {
namespace {

constexpr int kMaxGeneratedDim = 4096;

std::string LowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Values with spaces can't survive the whitespace token grammar — the
// font family is percent-encoded ("Segoe%20UI"). Only unreserved
// characters pass through untouched.
std::string PercentEncode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        const bool safe = (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' ||
                          c == '_' || c == '~' || c == '-';
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

std::string PercentDecode(const std::string& text) {
    const auto nybble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        const int lo = std::tolower(static_cast<unsigned char>(c));
        if (lo >= 'a' && lo <= 'f') return lo - 'a' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = nybble(text[i + 1]);
            const int lo = nybble(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

float ParseFloat(const std::string& value, float fallback) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(v)) return fallback;
    return v;
}

int ParseIntValue(const std::string& value, int fallback) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return fallback;
    return static_cast<int>(v);
}

bool ParseBool(const std::string& value) {
    const std::string v = LowerAscii(value);
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

int ParseWeight(const std::string& value, int fallback) {
    const std::string v = LowerAscii(value);
    static const struct {
        const char* name;
        int weight;
    } kNames[] = {
        {"thin", 100},      {"hairline", 100}, {"extralight", 200},
        {"ultralight", 200},{"light", 300},    {"regular", 400},
        {"normal", 400},    {"medium", 500},   {"semibold", 600},
        {"demibold", 600},  {"bold", 700},     {"extrabold", 800},
        {"ultrabold", 800}, {"heavy", 800},    {"black", 900},
    };
    for (const auto& named : kNames) {
        if (v == named.name) return named.weight;
    }
    const int n = ParseIntValue(value, -1);
    return n > 0 ? n : fallback;
}

// Shortest fixed-point form that round-trips through strtof
// ("1.5", "0", "-2.25"); avoids float-noise tails like 0.30000001.
std::string FmtFloat(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string FmtHex(const std::array<std::uint8_t, 4>& bgra) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", bgra[2],
                  bgra[1], bgra[0], bgra[3]);
    return buf;
}

// Ensures `out` is a valid w*h BGRA8 frame (reuses the allocation when
// it already matches).
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

std::vector<std::string> SplitTextLines(const std::string& utf8) {
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const char c = utf8[i];
        if (c == '\n') {
            lines.emplace_back();
        } else if (c == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') {
                ++i;
            }
            lines.emplace_back();
        } else {
            lines.back().push_back(c);
        }
    }
    return lines;
}

// Empty text is valid but invisible: a 1x1 transparent frame
// contributes nothing through the warp/blend stages.
bool EmptyFrame(video::Frame* out) {
    if (!EnsureFrameSize(out, 1, 1)) return false;
    std::memset(out->Data(), 0, out->DataSize());
    return true;
}

#if defined(XER_VE_HAS_SKIA)

// Minimal UTF-8 decoder — invalid bytes decode as U+FFFD and consume
// one byte, so a mangled param still lays out predictably.
std::vector<SkUnichar> DecodeUtf8(const std::string& utf8) {
    std::vector<SkUnichar> out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        SkUnichar cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0F) << 12) | ((utf8[i + 1] & 0x3F) << 6) |
                 (utf8[i + 2] & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) |
                 ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F);
            len = 4;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

sk_sp<SkFontMgr> TextFontMgr() {
    static const sk_sp<SkFontMgr> mgr = [] {
#if defined(XER_TEXT_STYLE_GDI_MGR)
        if (sk_sp<SkFontMgr> m = SkFontMgr_New_GDI()) {
            return m;
        }
#endif
        return SkFontMgr::RefEmpty();
    }();
    return mgr;
}

SkFontStyle MakeFontStyle(int weight, bool italic) {
    return SkFontStyle(
        std::clamp(weight, 1, 1000), SkFontStyle::kNormal_Width,
        italic ? SkFontStyle::kItalic_Slant
               : SkFontStyle::kUpright_Slant);
}

// Family + weight/slant resolve with two fallback stages: any face of
// the named family, then the platform default — a typo in `font=`
// must degrade to the system face, never to invisible text.
sk_sp<SkTypeface> ResolveTypeface(SkFontMgr* mgr, const std::string& family,
                                  int weight, bool italic) {
    if (mgr == nullptr) return nullptr;
    const SkFontStyle fs = MakeFontStyle(weight, italic);
    sk_sp<SkTypeface> tf;
    if (!family.empty()) {
        tf = mgr->matchFamilyStyle(family.c_str(), fs);
    }
    if (!tf) {
        tf = mgr->legacyMakeTypeface(family.empty() ? nullptr
                                                  : family.c_str(),
                                     fs);
    }
    if (!tf) {
        tf = mgr->legacyMakeTypeface(nullptr, fs);
    }
    return tf;
}

SkColor BgraToSkColor(const std::array<std::uint8_t, 4>& bgra,
                    float opacity = 1.0f) {
    const int a = std::clamp(
        static_cast<int>(std::lround(bgra[3] * opacity)), 0, 255);
    return SkColorSetARGB(static_cast<U8CPU>(a), bgra[2], bgra[1],
                          bgra[0]);
}

struct LaidOutLine {
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> pos;        // pen origin per glyph (y = 0,
                                     // baseline added at draw time)
    std::vector<bool> use_fallback;  // per glyph: drawn with `fallback`
    // Whitespace-delimited word index per glyph (-1 = separator).
    // Words number up ACROSS lines — the karaoke word_times table
    // indexes this — and a hard '\n' always breaks a word.
    std::vector<int> word_index;
    float width = 0.0f;
};

// Shapes one line into glyph ids + pen positions. Missing glyphs hop
// to `fallback` so mixed-script titles don't tofu out; `tracking` is
// appended after every glyph. `word_counter` is in/out and keeps
// counting across lines (the caller seeds it at -1 before line 0).
LaidOutLine LayOutLine(const std::string& line, const SkFont& font,
                       const SkFont& fallback, float tracking,
                       int* word_counter) {
    LaidOutLine out;
    const std::vector<SkUnichar> chars = DecodeUtf8(line);
    out.glyphs.reserve(chars.size());
    out.use_fallback.reserve(chars.size());
    out.word_index.reserve(chars.size());
    bool in_word = false;   // '\n' split already reset this per line
    for (const SkUnichar u : chars) {
        const bool sep = (u == 0x20 || u == 0x09);
        if (sep) {
            out.word_index.push_back(-1);
            in_word = false;
        } else {
            if (!in_word) ++(*word_counter);
            in_word = true;
            out.word_index.push_back(*word_counter);
        }
        SkGlyphID glyph = font.unicharToGlyph(u);
        bool fb = false;
        if (glyph == 0) {
            const SkGlyphID g2 = fallback.unicharToGlyph(u);
            if (g2 != 0) {
                glyph = g2;
                fb = true;
            }
        }
        out.glyphs.push_back(glyph);
        out.use_fallback.push_back(fb);
    }
    std::vector<SkScalar> widths(out.glyphs.size());
    // Advance queries must hit the face the glyph came from — a
    // fallback glyph's width is meaningless in the primary font.
    std::size_t i = 0;
    while (i < out.glyphs.size()) {
        const bool fb = out.use_fallback[i];
        std::size_t j = i;
        while (j < out.glyphs.size() && out.use_fallback[j] == fb) {
            ++j;
        }
        const SkFont& used = fb ? fallback : font;
        used.getWidths(
            {out.glyphs.data() + i, j - i},
            {widths.data() + i, j - i});
        i = j;
    }
    out.pos.resize(out.glyphs.size());
    SkScalar x = 0;
    for (std::size_t g = 0; g < out.glyphs.size(); ++g) {
        out.pos[g] = {x, 0};
        x += widths[g] + tracking;
    }
    out.width = out.glyphs.empty() ? 0.0f : x - tracking;
    return out;
}

bool RenderClipTextSkia(const ClipTextStyle& in_style, int canvas_h,
                        double clip_local_t, video::Frame* out) {
    if (in_style.text.empty()) {
        return EmptyFrame(out);
    }
    // Mutable copy: the shadow/stroke passes suppress the decoration
    // rects (they only read right in the fill pass).
    ClipTextStyle style = in_style;
    sk_sp<SkFontMgr> mgr = TextFontMgr();
    const float size_px =
        style.size_px > 0
            ? static_cast<float>(style.size_px)
            : static_cast<float>(std::clamp(canvas_h / 16, 8, 512));
    SkFont font(ResolveTypeface(mgr.get(), style.font_family,
                                style.font_weight, style.italic),
                size_px);
    // Grayscale AA, not subpixel: these pixels get alpha-blended onto
    // arbitrary footage, where LCD fringing is wrong.
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setSubpixel(true);
    SkFont fallback(
        mgr ? mgr->legacyMakeTypeface(nullptr,
                                      MakeFontStyle(style.font_weight,
                                                    style.italic))
            : nullptr,
        size_px);
    fallback.setEdging(SkFont::Edging::kAntiAlias);
    fallback.setSubpixel(true);
    if (!font.getTypeface() && !fallback.getTypeface()) {
        // No fonts at all (empty fontmgr) — produce a transparent box
        // rather than failing the clip.
        const int w = std::clamp(
            static_cast<int>(std::lround(style.box_w)), 1, 256);
        const int h = std::clamp(
            static_cast<int>(std::lround(style.box_h)), 1, 256);
        if (!EnsureFrameSize(out, w, h)) return false;
        std::memset(out->Data(), 0, out->DataSize());
        return true;
    }

    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    const float line_adv =
        style.line_spacing > 0.0f
            ? style.line_spacing
            : (metrics.fDescent - metrics.fAscent + metrics.fLeading);

    const std::vector<std::string> lines = SplitTextLines(style.text);
    std::vector<LaidOutLine> laid;
    laid.reserve(lines.size());
    float block_w = 0.0f;
    int word_counter = -1;
    for (const std::string& line : lines) {
        laid.push_back(LayOutLine(line, font, fallback,
                                  style.letter_spacing,
                                  &word_counter));
        block_w = std::max(block_w, laid.back().width);
    }

    // Karaoke fill buckets: 0 = base color (inactive — and "done"
    // when no done color is set), 1 = active word, 2 = done words.
    const bool karaoke_on =
        style.karaoke && !style.word_times.empty();
    const auto bucket_of = [&](int wi) -> int {
        if (!karaoke_on || wi < 0 ||
            wi >= static_cast<int>(style.word_times.size())) {
            return 0;
        }
        const auto& ws =
            style.word_times[static_cast<std::size_t>(wi)];
        if (clip_local_t >= ws.t0 && clip_local_t < ws.t1) return 1;
        if (clip_local_t >= ws.t1) {
            return style.karaoke_done[3] > 0 ? 2 : 0;
        }
        return 0;
    };
    const float block_h =
        (-metrics.fAscent + metrics.fDescent) +
        (laid.size() > 0 ? (laid.size() - 1) * line_adv : 0.0f);

    // Box geometry in "box space": the box sits at (0,0,area_w,area_h);
    // the text block starts at (inset_x, inset_y) inside it. With no
    // explicit box the area fits the text plus insets; negative insets
    // only extend the frame (never silently clip).
    float inset_x = style.pos_x;
    float inset_y = style.pos_y;
    if (style.box_w <= 0.0f) inset_x = std::max(0.0f, inset_x);
    if (style.box_h <= 0.0f) inset_y = std::max(0.0f, inset_y);
    const float area_w =
        std::max(1.0f, style.box_w > 0.0f ? style.box_w
                                          : block_w + inset_x);
    const float area_h =
        std::max(1.0f, style.box_h > 0.0f ? style.box_h
                                          : block_h + inset_y);
    const float content_l = std::min(0.0f, inset_x);
    const float content_t = std::min(0.0f, inset_y);
    const float content_r = std::max(area_w, inset_x + block_w);
    const float content_b = std::max(area_h, inset_y + block_h);

    // Shadow resolution — see the struct comment for the auto/legacy
    // rules (implicit shadow iff an outline isn't already drawing one).
    const bool shadow_on =
        style.shadow_explicit
            ? (style.shadow_color[3] > 0)
            : (style.outline_px <= 0.0f);
    const float sh_dx =
        style.shadow_explicit
            ? style.shadow_x
            : std::max(1.0f, size_px * 0.07f);
    const float sh_dy =
        style.shadow_explicit
            ? style.shadow_y
            : std::max(1.0f, size_px * 0.07f);
    const float sh_blur =
        style.shadow_explicit ? style.shadow_blur
                              : size_px * 0.09f;
    const auto shadow_bgra =
        style.shadow_explicit
            ? style.shadow_color
            : std::array<std::uint8_t, 4>{0, 0, 0, 128};

    const float stroke_ext = std::max(0.0f, style.outline_px) + 1.0f;
    const float shadow_ext =
        shadow_on ? (std::fabs(sh_dx) + sh_blur * 2.0f +
                     std::fabs(sh_dy) + sh_blur * 2.0f) *
                        0.5f +
                        2.0f
                  : 0.0f;
    float pad = 2.0f + std::max(stroke_ext, shadow_ext);
    // Rotation can swing box corners far outside the unrotated bounds —
    // pad up to half the diagonal so nothing clips.
    if (style.rotate_deg != 0.0f) {
        pad += 0.5f * std::hypot(area_w, area_h);
    }

    const double need_w =
        std::ceil(content_r - content_l + 2.0 * pad);
    const double need_h =
        std::ceil(content_b - content_t + 2.0 * pad);
    if (need_w > kMaxGeneratedDim || need_h > kMaxGeneratedDim ||
        need_w <= 0 || need_h <= 0) {
        return false;
    }
    const int w = static_cast<int>(need_w);
    const int h = static_cast<int>(need_h);

    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h));
    if (!surface) return false;
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    // Origin maps box space (content_l,content_t) to (pad,pad).
    const float ox = pad - content_l;
    const float oy = pad - content_t;

    if (style.bg_color[3] > 0) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(BgraToSkColor(style.bg_color));
        const SkRect box =
            SkRect::MakeXYWH(ox, oy, area_w, area_h);
        if (style.corner_radius > 0.0f) {
            canvas->drawRoundRect(box, style.corner_radius,
                                  style.corner_radius, paint);
        } else {
            canvas->drawRect(box, paint);
        }
    }

    canvas->save();
    if (style.rotate_deg != 0.0f) {
        canvas->rotate(style.rotate_deg, ox + area_w * 0.5f,
                       oy + area_h * 0.5f);
    }

    // Per-line anchor: horizontal align inside [inset_x, area_w),
    // vertical align inside [inset_y, area_h).
    const float region_w = std::max(0.0f, area_w - inset_x);
    const float region_h = std::max(0.0f, area_h - inset_y);
    const float block_y =
        oy + inset_y +
        (style.valign == ClipTextStyle::VAlign::Middle
             ? (region_h - block_h) * 0.5f
             : style.valign == ClipTextStyle::VAlign::Bottom
                   ? (region_h - block_h)
                   : 0.0f);

    // Glyph pass driver: `paints` holds 1..3 colors — bucket 0 is
    // always the pass color; karaoke fills pass all three so a glyph
    // run is additionally split on its word's timing bucket. The
    // shadow/stroke passes stay single-paint (one run split per font
    // change, exactly as before).
    auto draw_glyphs = [&](SkPaint* paints, int paint_count,
                           bool decorations) {
        for (std::size_t li = 0; li < laid.size(); ++li) {
            const LaidOutLine& line = laid[li];
            if (line.glyphs.empty()) continue;
            const float align_off =
                style.align == ClipTextStyle::Align::Center
                    ? (region_w - line.width) * 0.5f
                    : style.align == ClipTextStyle::Align::Right
                          ? (region_w - line.width)
                          : 0.0f;
            const float base_y = block_y + (-metrics.fAscent) +
                                 style.baseline_px +
                                 static_cast<float>(li) * line_adv;
            const float left = ox + inset_x + align_off;
            std::vector<SkPoint> pos(line.pos.size());
            for (std::size_t g = 0; g < line.pos.size(); ++g) {
                pos[g] = {line.pos[g].x() + left,
                          line.pos[g].y() + base_y};
            }
            std::size_t i = 0;
            while (i < line.glyphs.size()) {
                const bool fb = line.use_fallback[i];
                const int bucket =
                    paint_count > 1 ? bucket_of(line.word_index[i])
                                    : 0;
                std::size_t j = i;
                while (j < line.glyphs.size() &&
                       line.use_fallback[j] == fb &&
                       (paint_count <= 1 ||
                        bucket_of(line.word_index[j]) == bucket)) {
                    ++j;
                }
                canvas->drawGlyphs(
                    SkSpan<const SkGlyphID>(line.glyphs.data() + i,
                                            j - i),
                    SkSpan<const SkPoint>(pos.data() + i, j - i),
                    {0, 0}, fb ? fallback : font,
                    paints[std::clamp(bucket, 0, paint_count - 1)]);
                i = j;
            }
            // Decorations ride on the fill pass' paint — they inherit
            // the fill color and are suppressed in shadow/stroke.
            const float line_right = left + line.width;
            SkScalar mthick = 0, mpos = 0;
            if (decorations && style.underline) {
                const float thick =
                    metrics.hasUnderlineThickness(&mthick)
                        ? mthick
                        : std::max(1.0f, size_px / 14.0f);
                const float uy =
                    base_y +
                    (metrics.hasUnderlinePosition(&mpos)
                         ? mpos
                         : size_px * 0.08f);
                canvas->drawRect(
                    SkRect::MakeLTRB(left, uy, line_right, uy + thick),
                    paints[0]);
            }
            if (decorations && style.strike) {
                const float thick =
                    metrics.hasStrikeoutThickness(&mthick)
                        ? mthick
                        : std::max(1.0f, size_px / 16.0f);
                const float sy =
                    base_y +
                    (metrics.hasStrikeoutPosition(&mpos)
                         ? mpos
                         : -size_px * 0.28f) -
                    thick * 0.5f;
                canvas->drawRect(
                    SkRect::MakeLTRB(left, sy, line_right, sy + thick),
                    paints[0]);
            }
        }
    };

    if (shadow_on) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(BgraToSkColor(shadow_bgra));
        if (sh_blur > 0.0f) {
            paint.setMaskFilter(SkMaskFilter::MakeBlur(
                kNormal_SkBlurStyle, sh_blur * 0.5f));
        }
        canvas->save();
        canvas->translate(sh_dx, sh_dy);
        draw_glyphs(&paint, 1, false);
        canvas->restore();
    }
    if (style.outline_px > 0.0f) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(style.outline_px);
        paint.setColor(BgraToSkColor(style.outline_color));
        draw_glyphs(&paint, 1, false);
    }
    {
        // Fill — up to three paints so the karaoke pass can split
        // glyph runs into inactive / active / done colors.
        SkPaint fills[3];
        fills[0].setAntiAlias(true);
        fills[0].setColor(
            BgraToSkColor(style.color, style.fill_opacity));
        int fill_count = 1;
        if (karaoke_on) {
            fills[1] = fills[0];
            fills[1].setColor(BgraToSkColor(style.karaoke_active,
                                            style.fill_opacity));
            fills[2] = fills[0];
            if (style.karaoke_done[3] > 0) {
                fills[2].setColor(BgraToSkColor(style.karaoke_done,
                                                style.fill_opacity));
            }
            fill_count = 3;
        }
        draw_glyphs(fills, fill_count, true);
    }
    canvas->restore();

    if (!EnsureFrameSize(out, w, h)) return false;
    // The pipeline contract is STRAIGHT alpha — read back unpremul.
    // If this skia build refuses an unpremul dst, fall back to a premul
    // read + manual conversion.
    const SkImageInfo dst_info = SkImageInfo::Make(
        w, h, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
    if (surface->readPixels(dst_info, out->Data(),
                            static_cast<size_t>(w) * 4, 0, 0)) {
        return true;
    }
    std::vector<std::uint8_t> premul(static_cast<size_t>(w) * h * 4);
    const SkImageInfo pm_info = SkImageInfo::Make(
        w, h, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    if (!surface->readPixels(pm_info, premul.data(),
                             static_cast<size_t>(w) * 4, 0, 0)) {
        return false;
    }
    std::uint8_t* dst = out->Data();
    for (size_t p = 0; p < premul.size(); p += 4) {
        const unsigned a = premul[p + 3];
        if (a == 0) {
            dst[p] = dst[p + 1] = dst[p + 2] = dst[p + 3] = 0;
            continue;
        }
        dst[p + 0] = static_cast<std::uint8_t>(
            std::min(255u, premul[p + 0] * 255u / a));
        dst[p + 1] = static_cast<std::uint8_t>(
            std::min(255u, premul[p + 1] * 255u / a));
        dst[p + 2] = static_cast<std::uint8_t>(
            std::min(255u, premul[p + 2] * 255u / a));
        dst[p + 3] = static_cast<std::uint8_t>(a);
    }
    return true;
}

#else   // !XER_VE_HAS_SKIA — BitmapFont fallback (legacy subset)

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

constexpr std::array<std::uint8_t, 4> kShadowNear{0, 0, 0, 120};
constexpr std::array<std::uint8_t, 4> kShadowFar{0, 0, 0, 45};

bool RenderClipTextBitmap(const ClipTextStyle& style, int canvas_h,
                          double clip_local_t, video::Frame* out) {
    if (style.text.empty()) {
        return EmptyFrame(out);
    }
    const int scale = ScaleForTextStyle(style, canvas_h);
    const std::vector<std::string> lines = SplitTextLines(style.text);
    std::vector<int> line_w(lines.size(), 0);
    std::int64_t block_w = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        int line_h = 0;
        if (!BitmapFont::MeasureText(lines[i], scale, &line_w[i],
                                     &line_h)) {
            return false;
        }
        block_w = std::max<std::int64_t>(block_w, line_w[i]);
    }
    const std::int64_t text_h =
        (static_cast<std::int64_t>(lines.size()) *
             BitmapFont::kLineAdvance - 2) *
        scale;
    const std::int64_t edge =
        style.outline_px > 0.0f
            ? std::max<std::int64_t>(
                  1, std::lround(
                         static_cast<double>(style.outline_px)))
            : 0;
    const std::int64_t shadow = edge == 0 ? 2 * scale : 0;
    const std::int64_t need_w = block_w + 2 * edge + shadow;
    const std::int64_t need_h = text_h + 2 * edge + shadow;
    if (need_w > kMaxGeneratedDim || need_h > kMaxGeneratedDim) {
        return false;
    }
    const int w = static_cast<int>(std::max<std::int64_t>(need_w, 1));
    const int h = static_cast<int>(std::max<std::int64_t>(need_h, 1));
    if (!EnsureFrameSize(out, w, h)) return false;
    std::memset(out->Data(), 0, out->DataSize());

    const int edge_px = static_cast<int>(edge);
    const auto xoff_of = [&](std::size_t i) -> int {
        if (style.align == ClipTextStyle::Align::Center) {
            return static_cast<int>((block_w - line_w[i]) / 2);
        }
        if (style.align == ClipTextStyle::Align::Right) {
            return static_cast<int>(block_w - line_w[i]);
        }
        return 0;
    };
    auto draw_pass = [&](int dx, int dy,
                         const std::array<std::uint8_t, 4>& bgra) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            BitmapFont::DrawText(
                out, lines[i], edge_px + xoff_of(i) + dx,
                edge_px + dy + static_cast<int>(i) *
                                   BitmapFont::kLineAdvance * scale,
                scale, bgra);
        }
    };
    // Karaoke fill: same bucket math as the Skia path — word i is
    // active inside its clip-local span, "done" after it (falling
    // back to base color when no done color is set). Drawn as maximal
    // same-color runs; the bitmap face is monospace so a run's x
    // offset is column * advance * scale and spaces merge into
    // either neighbor (they draw no pixels anyway).
    const bool karaoke_on =
        style.karaoke && !style.word_times.empty();
    const auto bucket_of = [&](int wi) -> int {
        if (!karaoke_on || wi < 0 ||
            wi >= static_cast<int>(style.word_times.size())) {
            return 0;
        }
        const auto& ws =
            style.word_times[static_cast<std::size_t>(wi)];
        if (clip_local_t >= ws.t0 && clip_local_t < ws.t1) return 1;
        if (clip_local_t >= ws.t1) {
            return style.karaoke_done[3] > 0 ? 2 : 0;
        }
        return 0;
    };
    auto draw_karaoke_fill = [&](int dx, int dy) {
        const std::array<std::uint8_t, 4> colors[3] = {
            style.color, style.karaoke_active,
            style.karaoke_done[3] > 0 ? style.karaoke_done
                                      : style.color,
        };
        int word_counter = -1;   // counts across lines ('\n' breaks)
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            const int base_x = edge_px + xoff_of(i) + dx;
            const int y =
                edge_px + dy +
                static_cast<int>(i) * BitmapFont::kLineAdvance *
                    scale;
            std::size_t byte_i = 0;
            int cp_col = 0;          // codepoint column of byte_i
            std::size_t run_byte0 = 0;
            int run_cp0 = 0;
            int run_bucket = 0;
            int cur_bucket = 0;      // last word's bucket (seps merge)
            bool in_word = false;    // '\n' ends a word too
            const auto flush = [&](std::size_t byte_end,
                                   int cp_end) {
                if (byte_end <= run_byte0) return;
                BitmapFont::DrawText(
                    out,
                    line.substr(run_byte0, byte_end - run_byte0),
                    base_x + run_cp0 * BitmapFont::kGlyphAdvance *
                                 scale,
                    y, scale, colors[run_bucket]);
                run_byte0 = byte_end;
                run_cp0 = cp_end;
            };
            while (byte_i < line.size()) {
                const unsigned char c =
                    static_cast<unsigned char>(line[byte_i]);
                std::size_t cp_len = 1;
                if (c >= 0xC0) {
                    if ((c & 0xE0) == 0xC0) {
                        cp_len = 2;
                    } else if ((c & 0xF0) == 0xE0) {
                        cp_len = 3;
                    } else if ((c & 0xF8) == 0xF0) {
                        cp_len = 4;
                    }
                    cp_len = std::min(cp_len, line.size() - byte_i);
                }
                const bool sep = (c == ' ' || c == '\t');
                if (sep) {
                    in_word = false;
                } else {
                    if (!in_word) ++word_counter;
                    in_word = true;
                    cur_bucket = bucket_of(word_counter);
                    if (cur_bucket != run_bucket) {
                        flush(byte_i, cp_col);
                        run_bucket = cur_bucket;
                    }
                }
                byte_i += cp_len;
                ++cp_col;
            }
            flush(byte_i, cp_col);
        }
    };
    if (edge_px > 0) {
        static const int kDirs[8][2] = {
            {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
            {1, 0},   {-1, 1}, {0, 1},  {1, 1},
        };
        for (const auto& dir : kDirs) {
            draw_pass(dir[0] * edge_px, dir[1] * edge_px,
                      style.outline_color);
        }
    } else if (!style.shadow_explicit ||
               style.shadow_color[3] > 0) {
        if (style.shadow_explicit) {
            draw_pass(static_cast<int>(style.shadow_x),
                      static_cast<int>(style.shadow_y),
                      style.shadow_color);
        } else {
            draw_pass(2 * scale, 2 * scale, kShadowFar);
            draw_pass(scale, scale, kShadowNear);
        }
    }
    if (karaoke_on) {
        draw_karaoke_fill(0, 0);
    } else {
        draw_pass(0, 0, style.color);
    }
    return true;
}

#endif  // XER_VE_HAS_SKIA

}  // namespace

ClipTextStyle ParseClipTextStyle(const std::string& param) {
    ClipTextStyle style;
    const std::size_t bar = param.find('|');
    if (bar == std::string::npos) {
        style.text = param;
        return style;
    }
    style.text = param.substr(0, bar);
    while (!style.text.empty() &&
           (style.text.back() == ' ' || style.text.back() == '\t')) {
        style.text.pop_back();
    }
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
        } else if (key == "font" || key == "family") {
            style.font_family = PercentDecode(value);
        } else if (key == "weight") {
            style.font_weight = std::clamp(
                ParseWeight(value, style.font_weight), 1, 1000);
        } else if (key == "bold") {
            if (ParseBool(value)) style.font_weight = 700;
        } else if (key == "italic") {
            style.italic = ParseBool(value);
        } else if (key == "underline") {
            style.underline = ParseBool(value);
        } else if (key == "strike" || key == "strikethrough") {
            style.strike = ParseBool(value);
        } else if (key == "tracking" || key == "letterspacing") {
            style.letter_spacing =
                std::clamp(ParseFloat(value, 0.0f), -256.0f, 2048.0f);
        } else if (key == "leading" || key == "linespacing") {
            style.line_spacing =
                std::clamp(ParseFloat(value, 0.0f), 0.0f, 4096.0f);
        } else if (key == "align") {
            const std::string a = LowerAscii(value);
            if (a == "center") {
                style.align = ClipTextStyle::Align::Center;
            } else if (a == "right") {
                style.align = ClipTextStyle::Align::Right;
            } else if (a == "left") {
                style.align = ClipTextStyle::Align::Left;
            }
        } else if (key == "valign") {
            const std::string a = LowerAscii(value);
            if (a == "middle" || a == "center" || a == "mid") {
                style.valign = ClipTextStyle::VAlign::Middle;
            } else if (a == "bottom") {
                style.valign = ClipTextStyle::VAlign::Bottom;
            } else if (a == "top") {
                style.valign = ClipTextStyle::VAlign::Top;
            }
        } else if (key == "x") {
            style.pos_x =
                std::clamp(ParseFloat(value, 0.0f), -8192.0f, 8192.0f);
        } else if (key == "y") {
            style.pos_y =
                std::clamp(ParseFloat(value, 0.0f), -8192.0f, 8192.0f);
        } else if (key == "w" || key == "width") {
            style.box_w =
                std::clamp(ParseFloat(value, 0.0f), 0.0f, 4096.0f);
        } else if (key == "h" || key == "height") {
            style.box_h =
                std::clamp(ParseFloat(value, 0.0f), 0.0f, 4096.0f);
        } else if (key == "radius") {
            style.corner_radius =
                std::clamp(ParseFloat(value, 0.0f), 0.0f, 2048.0f);
        } else if (key == "rotate" || key == "rotation") {
            style.rotate_deg =
                std::clamp(ParseFloat(value, 0.0f), -360.0f, 360.0f);
        } else if (key == "baseline") {
            style.baseline_px =
                std::clamp(ParseFloat(value, 0.0f), -4096.0f, 4096.0f);
        } else if (key == "color" || key == "fill") {
            style.color = ParseMatteColor(value);
        } else if (key == "opacity") {
            float v = ParseFloat(value, 1.0f);
            if (v > 1.0f && v <= 100.0f) v /= 100.0f;
            style.fill_opacity = std::clamp(v, 0.0f, 1.0f);
        } else if (key == "outline" || key == "stroke" ||
                   key == "stroke_width") {
            style.outline_px = std::clamp(
                std::max(0.0f, ParseFloat(value, 0.0f)), 0.0f, 512.0f);
        } else if (key == "outline_color" || key == "stroke_color") {
            style.outline_color = ParseMatteColor(value);
        } else if (key == "bg" || key == "background" ||
                   key == "bg_color") {
            style.bg_color = ParseMatteColor(value);
        } else if (key == "shadow") {
            style.shadow_explicit = true;
            const std::string v = LowerAscii(value);
            if (v == "0" || v == "none" || v == "off") {
                style.shadow_color = {0, 0, 0, 0};
            }
        } else if (key == "shadow_x" || key == "shadow_dx") {
            style.shadow_explicit = true;
            style.shadow_x =
                std::clamp(ParseFloat(value, 0.0f), -1024.0f, 1024.0f);
        } else if (key == "shadow_y" || key == "shadow_dy") {
            style.shadow_explicit = true;
            style.shadow_y =
                std::clamp(ParseFloat(value, 0.0f), -1024.0f, 1024.0f);
        } else if (key == "shadow_blur") {
            style.shadow_explicit = true;
            style.shadow_blur =
                std::clamp(ParseFloat(value, 0.0f), 0.0f, 512.0f);
        } else if (key == "shadow_color") {
            style.shadow_explicit = true;
            style.shadow_color = ParseMatteColor(value);
        } else if (key == "karaoke") {
            style.karaoke = ParseBool(value);
        } else if (key == "karaoke_times" || key == "kwt") {
            // "t0-t1,t0-t1,..." — clip-local word spans. Any parsed
            // pair flips karaoke on so kwt= alone lights the feature.
            std::size_t p = 0;
            while (p <= value.size()) {
                const std::size_t comma = value.find(',', p);
                const std::string pair = value.substr(
                    p, comma == std::string::npos
                           ? comma
                           : comma - p);
                const std::size_t dash = pair.find('-');
                if (dash != std::string::npos) {
                    const float a = ParseFloat(pair.substr(0, dash),
                                               -1.0f);
                    const float b = ParseFloat(pair.substr(dash + 1),
                                               -1.0f);
                    if (a >= 0.0f && b >= a) {
                        style.word_times.push_back({a, b});
                        style.karaoke = true;
                    }
                }
                if (comma == std::string::npos) break;
                p = comma + 1;
            }
        } else if (key == "karaoke_active" || key == "kact") {
            style.karaoke_active = ParseMatteColor(value);
            style.karaoke = true;
        } else if (key == "karaoke_done" || key == "kdone") {
            style.karaoke_done = ParseMatteColor(value);
            style.karaoke = true;
        }
    }
    return style;
}

std::string SerializeClipTextStyle(const ClipTextStyle& style) {
    std::string text = style.text;
    // '|' terminates the text payload — it cannot survive the grammar.
    text.erase(std::remove(text.begin(), text.end(), '|'), text.end());
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    static const char* kAlign[] = {"left", "center", "right"};
    static const char* kVAlign[] = {"top", "middle", "bottom"};
    std::string out = text + " | font=" + PercentEncode(style.font_family);
    out += " weight=" + std::to_string(style.font_weight);
    out += " italic=" + std::string(style.italic ? "1" : "0");
    out += " underline=" + std::string(style.underline ? "1" : "0");
    out += " strike=" + std::string(style.strike ? "1" : "0");
    out += " size=" + std::to_string(style.size_px);
    out += " tracking=" + FmtFloat(style.letter_spacing);
    out += " leading=" + FmtFloat(style.line_spacing);
    out += " align=";
    out += kAlign[std::clamp(static_cast<int>(style.align), 0, 2)];
    out += " valign=";
    out += kVAlign[std::clamp(static_cast<int>(style.valign), 0, 2)];
    out += " x=" + FmtFloat(style.pos_x);
    out += " y=" + FmtFloat(style.pos_y);
    out += " w=" + FmtFloat(style.box_w);
    out += " h=" + FmtFloat(style.box_h);
    out += " radius=" + FmtFloat(style.corner_radius);
    out += " rotate=" + FmtFloat(style.rotate_deg);
    out += " baseline=" + FmtFloat(style.baseline_px);
    out += " color=" + FmtHex(style.color);
    out += " opacity=" + FmtFloat(style.fill_opacity);
    out += " outline=" + FmtFloat(style.outline_px);
    out += " outline_color=" + FmtHex(style.outline_color);
    out += " bg=" + FmtHex(style.bg_color);
    out += " shadow=" + std::string(style.shadow_explicit ? "1" : "0");
    if (style.shadow_explicit) {
        out += " shadow_x=" + FmtFloat(style.shadow_x);
        out += " shadow_y=" + FmtFloat(style.shadow_y);
        out += " shadow_blur=" + FmtFloat(style.shadow_blur);
        out += " shadow_color=" + FmtHex(style.shadow_color);
    }
    if (style.karaoke) {
        out += " karaoke=1 kact=" + FmtHex(style.karaoke_active);
        if (style.karaoke_done[3] > 0) {
            out += " kdone=" + FmtHex(style.karaoke_done);
        }
        if (!style.word_times.empty()) {
            out += " kwt=";
            bool first = true;
            for (const auto& wt : style.word_times) {
                if (!first) out += ',';
                first = false;
                out += FmtFloat(wt.t0);
                out += '-';
                out += FmtFloat(wt.t1);
            }
        }
    }
    return out;
}

bool RenderClipText(const ClipTextStyle& style, int canvas_h,
                    video::Frame* out, double clip_local_t) {
    if (out == nullptr) return false;
#if defined(XER_VE_HAS_SKIA)
    return RenderClipTextSkia(style, canvas_h, clip_local_t, out);
#else
    return RenderClipTextBitmap(style, canvas_h, clip_local_t, out);
#endif
}

}  // namespace XER::VideoEditor
