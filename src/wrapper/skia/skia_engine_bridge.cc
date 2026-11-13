#include "helper/string_utils.h"
#include "wrapper/skia/skia_engine_bridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#if ENGINE_HAS_SKIA_BRIDGE
#if __has_include("include/core/SkColor.h")
#include "include/core/SkColor.h"
#include "include/core/SkBlendMode.h"
#define ENGINE_SKIA_NATIVE_INCLUDES 1
#elif __has_include(<include/core/SkColor.h>)
#include <include/core/SkColor.h>
#include <include/core/SkBlendMode.h>
#define ENGINE_SKIA_NATIVE_INCLUDES 1
#elif __has_include(<SkColor.h>)
#include <SkColor.h>
#include <SkBlendMode.h>
#define ENGINE_SKIA_NATIVE_INCLUDES 1
#else
#define ENGINE_SKIA_NATIVE_INCLUDES 0
#endif
#else
#define ENGINE_SKIA_NATIVE_INCLUDES 0
#endif

#if ENGINE_SKIA_NATIVE_INCLUDES
#if __has_include("include/core/SkBitmap.h") && __has_include("include/core/SkCanvas.h") && __has_include("include/core/SkPaint.h") && __has_include("include/core/SkRect.h") && __has_include("include/core/SkImageInfo.h") && __has_include("include/core/SkPath.h") && __has_include("include/core/SkPathBuilder.h") && __has_include("include/core/SkImage.h")
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#define ENGINE_SKIA_NATIVE_RASTER 1
#if __has_include("include/encode/SkPngEncoder.h") && __has_include("include/encode/SkJpegEncoder.h") && __has_include("include/codec/SkCodec.h") && __has_include("include/core/SkData.h") && __has_include("include/core/SkStream.h")
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/codec/SkCodec.h"
#define ENGINE_SKIA_NATIVE_CODEC 1
#else
#define ENGINE_SKIA_NATIVE_CODEC 0
#endif
#elif __has_include(<include/core/SkBitmap.h>) && __has_include(<include/core/SkCanvas.h>) && __has_include(<include/core/SkPaint.h>) && __has_include(<include/core/SkRect.h>) && __has_include(<include/core/SkImageInfo.h>) && __has_include(<include/core/SkPath.h>) && __has_include(<include/core/SkPathBuilder.h>) && __has_include(<include/core/SkImage.h>)
#include <include/core/SkBitmap.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkPath.h>
#include <include/core/SkPathBuilder.h>
#include <include/core/SkImage.h>
#include <include/core/SkPaint.h>
#include <include/core/SkRect.h>
#define ENGINE_SKIA_NATIVE_RASTER 1
#if __has_include(<include/encode/SkPngEncoder.h>) && __has_include(<include/encode/SkJpegEncoder.h>) && __has_include(<include/codec/SkCodec.h>) && __has_include(<include/core/SkData.h>) && __has_include(<include/core/SkStream.h>)
#include <include/core/SkData.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>
#include <include/encode/SkJpegEncoder.h>
#include <include/codec/SkCodec.h>
#define ENGINE_SKIA_NATIVE_CODEC 1
#else
#define ENGINE_SKIA_NATIVE_CODEC 0
#endif
#elif __has_include(<SkBitmap.h>) && __has_include(<SkCanvas.h>) && __has_include(<SkPaint.h>) && __has_include(<SkRect.h>) && __has_include(<SkImageInfo.h>) && __has_include(<SkPath.h>) && __has_include(<SkPathBuilder.h>) && __has_include(<SkImage.h>)
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkImageInfo.h>
#include <SkPath.h>
#include <SkPathBuilder.h>
#include <SkImage.h>
#include <SkPaint.h>
#include <SkRect.h>
#define ENGINE_SKIA_NATIVE_RASTER 1
#if __has_include(<SkPngEncoder.h>) && __has_include(<SkJpegEncoder.h>) && __has_include(<SkCodec.h>) && __has_include(<SkData.h>) && __has_include(<SkStream.h>)
#include <SkData.h>
#include <SkStream.h>
#include <SkPngEncoder.h>
#include <SkJpegEncoder.h>
#include <SkCodec.h>
#define ENGINE_SKIA_NATIVE_CODEC 1
#else
#define ENGINE_SKIA_NATIVE_CODEC 0
#endif
#else
#define ENGINE_SKIA_NATIVE_RASTER 0
#define ENGINE_SKIA_NATIVE_CODEC 0
#endif
#else
#define ENGINE_SKIA_NATIVE_RASTER 0
#define ENGINE_SKIA_NATIVE_CODEC 0
#endif

namespace XER::Bridge::Skia {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint8_t ClampByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

uint8_t GetR(uint32_t rgba) {
    return static_cast<uint8_t>((rgba >> 24) & 0xFF);
}

uint8_t GetG(uint32_t rgba) {
    return static_cast<uint8_t>((rgba >> 16) & 0xFF);
}

uint8_t GetB(uint32_t rgba) {
    return static_cast<uint8_t>((rgba >> 8) & 0xFF);
}

uint8_t GetA(uint32_t rgba) {
    return static_cast<uint8_t>(rgba & 0xFF);
}

uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(r) << 24)
        | (static_cast<uint32_t>(g) << 16)
        | (static_cast<uint32_t>(b) << 8)
        | static_cast<uint32_t>(a);
}

uint8_t MulDiv255(uint8_t v, uint8_t alpha) {
    return static_cast<uint8_t>((static_cast<uint16_t>(v) * static_cast<uint16_t>(alpha) + 127) / 255);
}

#if ENGINE_SKIA_NATIVE_INCLUDES
uint32_t PackFromSkColor(SkColor color) {
    return PackRGBA(SkColorGetR(color), SkColorGetG(color), SkColorGetB(color), SkColorGetA(color));
}

SkColor ToSkColor(uint32_t rgba) {
    return SkColorSetARGB(GetA(rgba), GetR(rgba), GetG(rgba), GetB(rgba));
}

bool TryMapBlendMode(const std::string& normalized, SkBlendMode* out_mode) {
    if (out_mode == nullptr) {
        return false;
    }
    if (normalized == "clear") { *out_mode = SkBlendMode::kClear; return true; }
    if (normalized == "src") { *out_mode = SkBlendMode::kSrc; return true; }
    if (normalized == "dst") { *out_mode = SkBlendMode::kDst; return true; }
    if (normalized == "srcOver") { *out_mode = SkBlendMode::kSrcOver; return true; }
    if (normalized == "dstOver") { *out_mode = SkBlendMode::kDstOver; return true; }
    if (normalized == "srcIn") { *out_mode = SkBlendMode::kSrcIn; return true; }
    if (normalized == "dstIn") { *out_mode = SkBlendMode::kDstIn; return true; }
    if (normalized == "srcOut") { *out_mode = SkBlendMode::kSrcOut; return true; }
    if (normalized == "dstOut") { *out_mode = SkBlendMode::kDstOut; return true; }
    if (normalized == "srcATop") { *out_mode = SkBlendMode::kSrcATop; return true; }
    if (normalized == "dstATop") { *out_mode = SkBlendMode::kDstATop; return true; }
    if (normalized == "xor") { *out_mode = SkBlendMode::kXor; return true; }
    if (normalized == "plus") { *out_mode = SkBlendMode::kPlus; return true; }
    if (normalized == "modulate") { *out_mode = SkBlendMode::kModulate; return true; }
    if (normalized == "screen") { *out_mode = SkBlendMode::kScreen; return true; }
    if (normalized == "overlay") { *out_mode = SkBlendMode::kOverlay; return true; }
    if (normalized == "darken") { *out_mode = SkBlendMode::kDarken; return true; }
    if (normalized == "lighten") { *out_mode = SkBlendMode::kLighten; return true; }
    if (normalized == "colorDodge") { *out_mode = SkBlendMode::kColorDodge; return true; }
    if (normalized == "colorBurn") { *out_mode = SkBlendMode::kColorBurn; return true; }
    if (normalized == "hardLight") { *out_mode = SkBlendMode::kHardLight; return true; }
    if (normalized == "softLight") { *out_mode = SkBlendMode::kSoftLight; return true; }
    if (normalized == "difference") { *out_mode = SkBlendMode::kDifference; return true; }
    if (normalized == "exclusion") { *out_mode = SkBlendMode::kExclusion; return true; }
    if (normalized == "multiply") { *out_mode = SkBlendMode::kMultiply; return true; }
    if (normalized == "hue") { *out_mode = SkBlendMode::kHue; return true; }
    if (normalized == "saturation") { *out_mode = SkBlendMode::kSaturation; return true; }
    if (normalized == "color") { *out_mode = SkBlendMode::kColor; return true; }
    if (normalized == "luminosity") { *out_mode = SkBlendMode::kLuminosity; return true; }
    return false;
}
#endif

#if ENGINE_SKIA_NATIVE_RASTER
bool BuildBitmapFromPixels(const std::vector<uint32_t>& pixels,
                           int width,
                           int height,
                           SkBitmap* out_bitmap) {
    if (out_bitmap == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels.size() != expected) {
        return false;
    }

    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(SkImageInfo::MakeN32Premul(width, height))) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            *bitmap.getAddr32(x, y) = ToSkColor(pixels[idx]);
        }
    }

    *out_bitmap = std::move(bitmap);
    return true;
}

void ExtractPixelsFromBitmap(const SkBitmap& bitmap, std::vector<uint32_t>* out_pixels) {
    if (out_pixels == nullptr) {
        return;
    }
    const int width = bitmap.width();
    const int height = bitmap.height();
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    out_pixels->assign(total, 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            (*out_pixels)[idx] = PackFromSkColor(bitmap.getColor(x, y));
        }
    }
}

bool SetupPaint(uint32_t color,
                const std::string& blend_mode,
                bool filled,
                SkPaint* out_paint) {
    if (out_paint == nullptr) {
        return false;
    }
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ToSkColor(color));
    paint.setStyle(filled ? SkPaint::kFill_Style : SkPaint::kStroke_Style);
    if (!filled) {
        paint.setStrokeWidth(1.0f);
    }
    SkBlendMode mode = SkBlendMode::kSrcOver;
    if (TryMapBlendMode(NormalizeBlendModeName(blend_mode), &mode)) {
        paint.setBlendMode(mode);
    }
    *out_paint = paint;
    return true;
}

bool DrawPathOnBitmap(SkBitmap* bitmap,
                      const SkPath& path,
                      uint32_t color,
                      const std::string& blend_mode,
                      bool filled) {
    if (bitmap == nullptr) {
        return false;
    }
    SkPaint paint;
    if (!SetupPaint(color, blend_mode, filled, &paint)) {
        return false;
    }
    SkCanvas canvas(*bitmap);
    canvas.drawPath(path, paint);
    return true;
}
#endif

const std::vector<std::string>& BlendModesStorage() {
    static const std::vector<std::string> kBlendModes = {
        "clear", "src", "dst", "srcOver", "dstOver", "srcIn", "dstIn", "srcOut", "dstOut",
        "srcATop", "dstATop", "xor", "plus", "modulate", "screen", "overlay", "darken", "lighten",
        "colorDodge", "colorBurn", "hardLight", "softLight", "difference", "exclusion", "multiply",
        "hue", "saturation", "color", "luminosity"
    };
    return kBlendModes;
}

const std::unordered_map<std::string, std::string>& BlendAliases() {
    static const std::unordered_map<std::string, std::string> kAliases = {
        {"srcover", "srcOver"}, {"src_over", "srcOver"},
        {"dstover", "dstOver"}, {"dst_over", "dstOver"},
        {"srcin", "srcIn"}, {"src_in", "srcIn"},
        {"dstin", "dstIn"}, {"dst_in", "dstIn"},
        {"srcout", "srcOut"}, {"src_out", "srcOut"},
        {"dstout", "dstOut"}, {"dst_out", "dstOut"},
        {"srcatop", "srcATop"}, {"src_atop", "srcATop"},
        {"dstatop", "dstATop"}, {"dst_atop", "dstATop"},
        {"colordodge", "colorDodge"}, {"color_dodge", "colorDodge"},
        {"colorburn", "colorBurn"}, {"color_burn", "colorBurn"},
        {"hardlight", "hardLight"}, {"hard_light", "hardLight"},
        {"softlight", "softLight"}, {"soft_light", "softLight"}
    };
    return kAliases;
}

std::vector<std::string> BuildExportedFunctions() {
    std::vector<std::string> names = {
        "isAvailable", "summary", "version", "functions", "blendModes", "hasFunction",
        "degToRad", "radToDeg", "clamp01", "lerp", "mapRange", "distance", "pointToString",
        "makeColor", "colorToHex", "parseColorHex", "premultiplyAlpha", "unpremultiplyAlpha",
        "normalizeBlendMode", "blendSrcOver", "blend",
        "matrixIdentity", "matrixTranslate", "matrixScale", "matrixRotate", "matrixMultiply", "matrixInvert"
    };
    for (const auto& mode : BlendModesStorage()) {
        names.push_back("blend:" + mode);
    }
    return names;
}

const std::vector<std::string>& ExportedFunctionsStorage() {
    static const std::vector<std::string> kFunctions = BuildExportedFunctions();
    return kFunctions;
}

}  // namespace

bool IsAvailable() {
#if ENGINE_HAS_SKIA_BRIDGE
    return true;
#else
    return false;
#endif
}

bool HasNativeIncludes() {
#if ENGINE_SKIA_NATIVE_INCLUDES
    return true;
#else
    return false;
#endif
}

bool HasNativeRaster() {
#if ENGINE_SKIA_NATIVE_RASTER
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    if (!IsAvailable()) {
        return "Skia bridge unavailable";
    }
    if (!HasNativeIncludes()) {
        return "Skia bridge enabled (native includes not found, fallback mode)";
    }
    return HasNativeRaster()
        ? "Skia bridge enabled (native includes + native raster integrated)"
        : "Skia bridge enabled (native includes integrated, raster fallback mode)";
}

std::string Version() {
    if (!IsAvailable()) {
        return "Skia bridge fallback API v1";
    }
    if (!HasNativeIncludes()) {
        return "Skia bridge API v1 + fallback includes";
    }
    return HasNativeRaster()
        ? "Skia bridge API v1 + native includes + native raster"
        : "Skia bridge API v1 + native includes";
}

std::vector<std::string> ExportedFunctionNames() {
    return ExportedFunctionsStorage();
}

bool HasFunction(const std::string& name) {
    const auto& functions = ExportedFunctionsStorage();
    return std::find(functions.begin(), functions.end(), name) != functions.end();
}

std::vector<std::string> BlendModeNames() {
    return BlendModesStorage();
}

double DegToRad(double degrees) {
    return degrees * kPi / 180.0;
}

double RadToDeg(double radians) {
    return radians * 180.0 / kPi;
}

double Clamp01(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double Lerp(double from, double to, double t) {
    return from + (to - from) * t;
}

double MapRange(double value,
                double in_min,
                double in_max,
                double out_min,
                double out_max) {
    const double denom = in_max - in_min;
    if (std::abs(denom) < 1e-12) {
        return out_min;
    }
    const double norm = (value - in_min) / denom;
    return out_min + norm * (out_max - out_min);
}

double Distance(double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

std::string PointToString(double x, double y) {
    std::ostringstream out;
    out << "Point(" << x << ", " << y << ")";
    return out.str();
}

uint32_t MakeColorRGBA(int r, int g, int b, int a) {
#if ENGINE_SKIA_NATIVE_INCLUDES
    const SkColor native = SkColorSetARGB(ClampByte(a), ClampByte(r), ClampByte(g), ClampByte(b));
    return PackFromSkColor(native);
#else
    return PackRGBA(ClampByte(r), ClampByte(g), ClampByte(b), ClampByte(a));
#endif
}

std::string ColorToHex(uint32_t rgba, bool include_alpha) {
    std::ostringstream out;
    out << '#'
        << std::uppercase
        << std::hex
        << std::setfill('0')
        << std::setw(2) << static_cast<int>(GetR(rgba))
        << std::setw(2) << static_cast<int>(GetG(rgba))
        << std::setw(2) << static_cast<int>(GetB(rgba));
    if (include_alpha) {
        out << std::setw(2) << static_cast<int>(GetA(rgba));
    }
    return out.str();
}

bool ParseColorHex(const std::string& value, uint32_t* rgba_out) {
    if (rgba_out == nullptr || value.empty()) {
        return false;
    }

    std::string s = value;
    if (s[0] == '#') {
        s.erase(s.begin());
    }

    if (!(s.size() == 6 || s.size() == 8)) {
        return false;
    }

    for (char ch : s) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    unsigned int raw = 0;
    std::istringstream in(s);
    in >> std::hex >> raw;
    if (in.fail()) {
        return false;
    }

    if (s.size() == 6) {
        const uint8_t r = static_cast<uint8_t>((raw >> 16) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((raw >> 8) & 0xFF);
        const uint8_t b = static_cast<uint8_t>(raw & 0xFF);
        *rgba_out = PackRGBA(r, g, b, 255);
        return true;
    }

    const uint8_t r = static_cast<uint8_t>((raw >> 24) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((raw >> 16) & 0xFF);
    const uint8_t b = static_cast<uint8_t>((raw >> 8) & 0xFF);
    const uint8_t a = static_cast<uint8_t>(raw & 0xFF);
    *rgba_out = PackRGBA(r, g, b, a);
    return true;
}

uint32_t PremultiplyAlpha(uint32_t rgba) {
    const uint8_t a = GetA(rgba);
    return PackRGBA(
        MulDiv255(GetR(rgba), a),
        MulDiv255(GetG(rgba), a),
        MulDiv255(GetB(rgba), a),
        a);
}

uint32_t UnpremultiplyAlpha(uint32_t rgba) {
    const uint8_t a = GetA(rgba);
    if (a == 0) {
        return PackRGBA(0, 0, 0, 0);
    }

    auto unpre = [a](uint8_t c) -> uint8_t {
        const int v = (static_cast<int>(c) * 255 + (a / 2)) / a;
        return ClampByte(v);
    };

    return PackRGBA(unpre(GetR(rgba)), unpre(GetG(rgba)), unpre(GetB(rgba)), a);
}

std::string NormalizeBlendModeName(const std::string& mode_name) {
    std::string key = XER::Helper::String::ToLowerAscii(mode_name);
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), key.end());

    const auto alias_it = BlendAliases().find(key);
    if (alias_it != BlendAliases().end()) {
        return alias_it->second;
    }

    for (const auto& mode : BlendModesStorage()) {
        if (XER::Helper::String::ToLowerAscii(mode) == key) {
            return mode;
        }
    }
    return "srcOver";
}

uint32_t BlendSrcOver(uint32_t dst_rgba, uint32_t src_rgba) {
    const uint8_t sr = GetR(src_rgba);
    const uint8_t sg = GetG(src_rgba);
    const uint8_t sb = GetB(src_rgba);
    const uint8_t sa = GetA(src_rgba);

    const uint8_t dr = GetR(dst_rgba);
    const uint8_t dg = GetG(dst_rgba);
    const uint8_t db = GetB(dst_rgba);
    const uint8_t da = GetA(dst_rgba);

    const int inv_sa = 255 - sa;
    const uint8_t out_a = static_cast<uint8_t>(sa + ((da * inv_sa + 127) / 255));
    const uint8_t out_r = static_cast<uint8_t>(sr + ((dr * inv_sa + 127) / 255));
    const uint8_t out_g = static_cast<uint8_t>(sg + ((dg * inv_sa + 127) / 255));
    const uint8_t out_b = static_cast<uint8_t>(sb + ((db * inv_sa + 127) / 255));
    return PackRGBA(out_r, out_g, out_b, out_a);
}

uint32_t BlendModeApply(const std::string& mode_name, uint32_t dst_rgba, uint32_t src_rgba) {
    const std::string mode = NormalizeBlendModeName(mode_name);
#if ENGINE_SKIA_NATIVE_INCLUDES
    SkBlendMode sk_mode = SkBlendMode::kSrcOver;
    if (TryMapBlendMode(mode, &sk_mode)) {
        if (sk_mode == SkBlendMode::kSrc) {
            return src_rgba;
        }
        if (sk_mode == SkBlendMode::kDst) {
            return dst_rgba;
        }
        if (sk_mode == SkBlendMode::kClear) {
            return 0;
        }
    }
    const SkColor src_native = ToSkColor(src_rgba);
    const SkColor dst_native = ToSkColor(dst_rgba);
    (void)src_native;
    (void)dst_native;
#endif
    if (mode == "src") {
        return src_rgba;
    }
    if (mode == "dst") {
        return dst_rgba;
    }
    if (mode == "clear") {
        return 0;
    }
    if (mode == "plus") {
        return PackRGBA(
            ClampByte(static_cast<int>(GetR(src_rgba)) + static_cast<int>(GetR(dst_rgba))),
            ClampByte(static_cast<int>(GetG(src_rgba)) + static_cast<int>(GetG(dst_rgba))),
            ClampByte(static_cast<int>(GetB(src_rgba)) + static_cast<int>(GetB(dst_rgba))),
            ClampByte(static_cast<int>(GetA(src_rgba)) + static_cast<int>(GetA(dst_rgba))));
    }
    if (mode == "multiply") {
        return PackRGBA(
            MulDiv255(GetR(src_rgba), GetR(dst_rgba)),
            MulDiv255(GetG(src_rgba), GetG(dst_rgba)),
            MulDiv255(GetB(src_rgba), GetB(dst_rgba)),
            std::max(GetA(src_rgba), GetA(dst_rgba)));
    }
    if (mode == "screen") {
        auto screen = [](uint8_t s, uint8_t d) {
            return static_cast<uint8_t>(255 - ((255 - s) * (255 - d) + 127) / 255);
        };
        return PackRGBA(
            screen(GetR(src_rgba), GetR(dst_rgba)),
            screen(GetG(src_rgba), GetG(dst_rgba)),
            screen(GetB(src_rgba), GetB(dst_rgba)),
            std::max(GetA(src_rgba), GetA(dst_rgba)));
    }

    return BlendSrcOver(dst_rgba, src_rgba);
}

Matrix3x3 MatrixIdentity() {
    return Matrix3x3{1.0, 0.0, 0.0,
                     0.0, 1.0, 0.0,
                     0.0, 0.0, 1.0};
}

Matrix3x3 MatrixTranslate(double tx, double ty) {
    return Matrix3x3{1.0, 0.0, tx,
                     0.0, 1.0, ty,
                     0.0, 0.0, 1.0};
}

Matrix3x3 MatrixScale(double sx, double sy) {
    return Matrix3x3{sx,  0.0, 0.0,
                     0.0, sy,  0.0,
                     0.0, 0.0, 1.0};
}

Matrix3x3 MatrixRotateDegrees(double degrees) {
    const double rad = DegToRad(degrees);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    return Matrix3x3{c,   -s,  0.0,
                     s,    c,  0.0,
                     0.0, 0.0, 1.0};
}

Matrix3x3 MatrixMultiply(const Matrix3x3& a, const Matrix3x3& b) {
    Matrix3x3 out{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a[static_cast<size_t>(row * 3 + k)] * b[static_cast<size_t>(k * 3 + col)];
            }
            out[static_cast<size_t>(row * 3 + col)] = sum;
        }
    }
    return out;
}

bool MatrixInvert(const Matrix3x3& m, Matrix3x3* out_inv) {
    if (out_inv == nullptr) {
        return false;
    }

    const double a = m[0], b = m[1], c = m[2];
    const double d = m[3], e = m[4], f = m[5];
    const double g = m[6], h = m[7], i = m[8];

    const double A = (e * i - f * h);
    const double B = -(d * i - f * g);
    const double C = (d * h - e * g);
    const double D = -(b * i - c * h);
    const double E = (a * i - c * g);
    const double F = -(a * h - b * g);
    const double G = (b * f - c * e);
    const double H = -(a * f - c * d);
    const double I = (a * e - b * d);

    const double det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-12) {
        return false;
    }

    const double inv_det = 1.0 / det;
    *out_inv = Matrix3x3{
        A * inv_det, D * inv_det, G * inv_det,
        B * inv_det, E * inv_det, H * inv_det,
        C * inv_det, F * inv_det, I * inv_det
    };
    return true;
}

bool RasterClear(std::vector<uint32_t>* pixels,
                 int width,
                 int height,
                 uint32_t color) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkCanvas canvas(bitmap);
    canvas.clear(ToSkColor(color));
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)color;
    return false;
#endif
}

bool RasterDrawRect(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    int x,
                    int y,
                    int rect_width,
                    int rect_height,
                    uint32_t color,
                    const std::string& blend_mode) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr || rect_width <= 0 || rect_height <= 0) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPaint paint;
    if (!SetupPaint(color, blend_mode, true, &paint)) {
        return false;
    }
    SkCanvas canvas(bitmap);
    canvas.drawRect(SkRect::MakeXYWH(static_cast<float>(x),
                                     static_cast<float>(y),
                                     static_cast<float>(rect_width),
                                     static_cast<float>(rect_height)),
                    paint);
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)x;
    (void)y;
    (void)rect_width;
    (void)rect_height;
    (void)color;
    (void)blend_mode;
    return false;
#endif
}

bool RasterDrawLine(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    int x0,
                    int y0,
                    int x1,
                    int y1,
                    uint32_t color,
                    const std::string& blend_mode) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPaint paint;
    if (!SetupPaint(color, blend_mode, false, &paint)) {
        return false;
    }
    SkCanvas canvas(bitmap);
    canvas.drawLine(static_cast<SkScalar>(x0),
                    static_cast<SkScalar>(y0),
                    static_cast<SkScalar>(x1),
                    static_cast<SkScalar>(y1),
                    paint);
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)color;
    (void)blend_mode;
    return false;
#endif
}

bool RasterDrawCircle(std::vector<uint32_t>* pixels,
                      int width,
                      int height,
                      int cx,
                      int cy,
                      int radius,
                      uint32_t color,
                      const std::string& blend_mode,
                      bool filled) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr || radius < 0) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPaint paint;
    if (!SetupPaint(color, blend_mode, filled, &paint)) {
        return false;
    }
    SkCanvas canvas(bitmap);
    canvas.drawCircle(static_cast<SkScalar>(cx),
                      static_cast<SkScalar>(cy),
                      static_cast<SkScalar>(radius),
                      paint);
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
    (void)blend_mode;
    (void)filled;
    return false;
#endif
}

bool RasterDrawTriangle(std::vector<uint32_t>* pixels,
                        int width,
                        int height,
                        int x1,
                        int y1,
                        int x2,
                        int y2,
                        int x3,
                        int y3,
                        uint32_t color,
                        const std::string& blend_mode,
                        bool filled) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPathBuilder builder;
    builder.moveTo(static_cast<SkScalar>(x1), static_cast<SkScalar>(y1));
    builder.lineTo(static_cast<SkScalar>(x2), static_cast<SkScalar>(y2));
    builder.lineTo(static_cast<SkScalar>(x3), static_cast<SkScalar>(y3));
    builder.close();
    const SkPath path = builder.detach();
    if (!DrawPathOnBitmap(&bitmap, path, color, blend_mode, filled)) {
        return false;
    }
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)color;
    (void)blend_mode;
    (void)filled;
    return false;
#endif
}

bool RasterDrawPolyline(std::vector<uint32_t>* pixels,
                        int width,
                        int height,
                        const std::vector<std::pair<int, int>>& points,
                        uint32_t color,
                        const std::string& blend_mode,
                        bool closed) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr || points.size() < 2) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPathBuilder builder;
    builder.moveTo(static_cast<SkScalar>(points[0].first), static_cast<SkScalar>(points[0].second));
    for (size_t i = 1; i < points.size(); ++i) {
        builder.lineTo(static_cast<SkScalar>(points[i].first), static_cast<SkScalar>(points[i].second));
    }
    if (closed) {
        builder.close();
    }
    const SkPath path = builder.detach();
    if (!DrawPathOnBitmap(&bitmap, path, color, blend_mode, false)) {
        return false;
    }
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)points;
    (void)color;
    (void)blend_mode;
    (void)closed;
    return false;
#endif
}

bool RasterFillPolygon(std::vector<uint32_t>* pixels,
                       int width,
                       int height,
                       const std::vector<std::pair<int, int>>& points,
                       uint32_t color,
                       const std::string& blend_mode) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr || points.size() < 3) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }
    SkPathBuilder builder;
    builder.moveTo(static_cast<SkScalar>(points[0].first), static_cast<SkScalar>(points[0].second));
    for (size_t i = 1; i < points.size(); ++i) {
        builder.lineTo(static_cast<SkScalar>(points[i].first), static_cast<SkScalar>(points[i].second));
    }
    builder.close();
    const SkPath path = builder.detach();
    if (!DrawPathOnBitmap(&bitmap, path, color, blend_mode, true)) {
        return false;
    }
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)points;
    (void)color;
    (void)blend_mode;
    return false;
#endif
}

bool RasterDrawImage(std::vector<uint32_t>* dst_pixels,
                     int dst_width,
                     int dst_height,
                     const std::vector<uint32_t>& src_pixels,
                     int src_width,
                     int src_height,
                     int dx,
                     int dy,
                     const std::string& blend_mode) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (dst_pixels == nullptr || src_width <= 0 || src_height <= 0) {
        return false;
    }

    SkBitmap dst_bitmap;
    if (!BuildBitmapFromPixels(*dst_pixels, dst_width, dst_height, &dst_bitmap)) {
        return false;
    }
    SkBitmap src_bitmap;
    if (!BuildBitmapFromPixels(src_pixels, src_width, src_height, &src_bitmap)) {
        return false;
    }

    SkPaint paint;
    if (!SetupPaint(0xFFFFFFFFu, blend_mode, true, &paint)) {
        return false;
    }

    SkCanvas canvas(dst_bitmap);
    const sk_sp<SkImage> src_image = src_bitmap.asImage();
    if (src_image != nullptr) {
        canvas.drawImage(src_image.get(), static_cast<SkScalar>(dx), static_cast<SkScalar>(dy),
                         SkSamplingOptions(), &paint);
    }
    ExtractPixelsFromBitmap(dst_bitmap, dst_pixels);
    return true;
#else
    (void)dst_pixels;
    (void)dst_width;
    (void)dst_height;
    (void)src_pixels;
    (void)src_width;
    (void)src_height;
    (void)dx;
    (void)dy;
    (void)blend_mode;
    return false;
#endif
}

bool RasterDrawPath(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    const PathData& path,
                    uint32_t color,
                    const std::string& blend_mode,
                    bool filled) {
#if ENGINE_SKIA_NATIVE_RASTER
    if (pixels == nullptr || path.empty()) {
        return false;
    }
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(*pixels, width, height, &bitmap)) {
        return false;
    }

    SkPathBuilder sk_path_builder;
    for (const auto& cmd : path) {
        switch (cmd.verb) {
            case PathVerb::kMove:
                if (cmd.pts.size() >= 2) {
                    sk_path_builder.moveTo(cmd.pts[0], cmd.pts[1]);
                }
                break;
            case PathVerb::kLine:
                if (cmd.pts.size() >= 2) {
                    sk_path_builder.lineTo(cmd.pts[0], cmd.pts[1]);
                }
                break;
            case PathVerb::kQuad:
                if (cmd.pts.size() >= 4) {
                    sk_path_builder.quadTo(cmd.pts[0], cmd.pts[1], cmd.pts[2], cmd.pts[3]);
                }
                break;
            case PathVerb::kConic:
                if (cmd.pts.size() >= 5) {
                    sk_path_builder.conicTo(cmd.pts[0], cmd.pts[1], cmd.pts[2], cmd.pts[3], cmd.pts[4]);
                }
                break;
            case PathVerb::kCubic:
                if (cmd.pts.size() >= 6) {
                    sk_path_builder.cubicTo(cmd.pts[0], cmd.pts[1],
                                    cmd.pts[2], cmd.pts[3],
                                    cmd.pts[4], cmd.pts[5]);
                }
                break;
            case PathVerb::kArcTo:
                if (cmd.pts.size() >= 7) {
                    // pts: rx ry xRotDeg largeArc(0/1) sweep(0/1) x y
                    const SkPathBuilder::ArcSize arc_size = (cmd.pts[3] != 0.f)
                        ? SkPathBuilder::kLarge_ArcSize
                        : SkPathBuilder::kSmall_ArcSize;
                    const SkPathDirection direction = (cmd.pts[4] != 0.f)
                        ? SkPathDirection::kCW
                        : SkPathDirection::kCCW;
                    sk_path_builder.arcTo(SkPoint::Make(cmd.pts[0], cmd.pts[1]),
                                  cmd.pts[2],
                                  arc_size,
                                  direction,
                                  SkPoint::Make(cmd.pts[5], cmd.pts[6]));
                }
                break;
            case PathVerb::kClose:
                sk_path_builder.close();
                break;
        }
    }
    const SkPath sk_path = sk_path_builder.detach();

    if (!DrawPathOnBitmap(&bitmap, sk_path, color, blend_mode, filled)) {
        return false;
    }
    ExtractPixelsFromBitmap(bitmap, pixels);
    return true;
#else
    (void)pixels;
    (void)width;
    (void)height;
    (void)path;
    (void)color;
    (void)blend_mode;
    (void)filled;
    return false;
#endif
}

bool RasterClipRect(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    int cx, int cy, int cw, int ch,
                    std::vector<uint32_t>* out_saved) {
    if (pixels == nullptr || out_saved == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels->size() != total) {
        return false;
    }
    // Save current pixel buffer, then zero out pixels outside clip rect
    *out_saved = *pixels;

    const int x0 = std::max(0, cx);
    const int y0 = std::max(0, cy);
    const int x1 = std::min(width, cx + std::max(0, cw));
    const int y1 = std::min(height, cy + std::max(0, ch));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x < x0 || x >= x1 || y < y0 || y >= y1) {
                (*pixels)[static_cast<size_t>(y) * static_cast<size_t>(width)
                          + static_cast<size_t>(x)] = 0;
            }
        }
    }
    return true;
}

bool RasterClipRestore(std::vector<uint32_t>* pixels,
                       int width,
                       int height,
                       const std::vector<uint32_t>& saved) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (saved.size() != total) {
        return false;
    }
    *pixels = saved;
    return true;
}

bool EncodeImagePNG(const std::vector<uint32_t>& pixels,
                    int width,
                    int height,
                    std::vector<uint8_t>* out_bytes) {
    if (out_bytes == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels.size() != expected) {
        return false;
    }
#if ENGINE_SKIA_NATIVE_RASTER && ENGINE_SKIA_NATIVE_CODEC
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(pixels, width, height, &bitmap)) {
        return false;
    }
    SkDynamicMemoryWStream stream;
    SkPngEncoder::Options opts;
    if (!SkPngEncoder::Encode(&stream, bitmap.pixmap(), opts)) {
        return false;
    }
    sk_sp<SkData> data = stream.detachAsData();
    if (!data) {
        return false;
    }
    out_bytes->assign(static_cast<const uint8_t*>(data->data()),
                      static_cast<const uint8_t*>(data->data()) + data->size());
    return true;
#else
    // Minimal fallback: write raw BGRA bytes prefixed with width/height
    out_bytes->clear();
    out_bytes->reserve(8 + pixels.size() * 4);
    auto push4 = [&](uint32_t v) {
        out_bytes->push_back(static_cast<uint8_t>(v & 0xFF));
        out_bytes->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out_bytes->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out_bytes->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    push4(static_cast<uint32_t>(width));
    push4(static_cast<uint32_t>(height));
    for (uint32_t px : pixels) {
        push4(px);
    }
    return true;
#endif
}

bool EncodeImageJPEG(const std::vector<uint32_t>& pixels,
                     int width,
                     int height,
                     int quality,
                     std::vector<uint8_t>* out_bytes) {
    if (out_bytes == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels.size() != expected) {
        return false;
    }
#if ENGINE_SKIA_NATIVE_RASTER && ENGINE_SKIA_NATIVE_CODEC
    SkBitmap bitmap;
    if (!BuildBitmapFromPixels(pixels, width, height, &bitmap)) {
        return false;
    }
    SkDynamicMemoryWStream stream;
    SkJpegEncoder::Options opts;
    opts.fQuality = std::max(0, std::min(100, quality));
    if (!SkJpegEncoder::Encode(&stream, bitmap.pixmap(), opts)) {
        return false;
    }
    sk_sp<SkData> data = stream.detachAsData();
    if (!data) {
        return false;
    }
    out_bytes->assign(static_cast<const uint8_t*>(data->data()),
                      static_cast<const uint8_t*>(data->data()) + data->size());
    return true;
#else
    (void)quality;
    return EncodeImagePNG(pixels, width, height, out_bytes);
#endif
}

bool DecodeImage(const std::vector<uint8_t>& bytes,
                 int* out_width,
                 int* out_height,
                 std::vector<uint32_t>* out_pixels) {
    if (out_width == nullptr || out_height == nullptr || out_pixels == nullptr || bytes.empty()) {
        return false;
    }
#if ENGINE_SKIA_NATIVE_RASTER && ENGINE_SKIA_NATIVE_CODEC
    auto data = SkData::MakeWithoutCopy(bytes.data(), bytes.size());
    if (!data) {
        return false;
    }
    auto codec = SkCodec::MakeFromData(data);
    if (!codec) {
        return false;
    }
    const SkImageInfo info = codec->getInfo().makeColorType(kN32_SkColorType).makeAlphaType(kPremul_SkAlphaType);
    *out_width = info.width();
    *out_height = info.height();
    const size_t row_bytes = static_cast<size_t>(info.minRowBytes());
    out_pixels->resize(static_cast<size_t>(info.width()) * static_cast<size_t>(info.height()));
    const SkCodec::Result result = codec->getPixels(info, out_pixels->data(), row_bytes);
    if (result != SkCodec::kSuccess) {
        out_pixels->clear();
        return false;
    }
    // Convert from N32 (BGRA premul on LE) → our RGBA layout
    for (auto& px : *out_pixels) {
        const uint32_t raw = px;
        const uint8_t b = static_cast<uint8_t>(raw & 0xFF);
        const uint8_t g = static_cast<uint8_t>((raw >> 8) & 0xFF);
        const uint8_t r = static_cast<uint8_t>((raw >> 16) & 0xFF);
        const uint8_t a = static_cast<uint8_t>((raw >> 24) & 0xFF);
        px = (static_cast<uint32_t>(r) << 24)
           | (static_cast<uint32_t>(g) << 16)
           | (static_cast<uint32_t>(b) << 8)
           | static_cast<uint32_t>(a);
    }
    return true;
#else
    // Minimal fallback: try to parse raw format written by EncodeImagePNG fallback
    if (bytes.size() < 8) {
        return false;
    }
    auto read4 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(bytes[off])
             | (static_cast<uint32_t>(bytes[off + 1]) << 8)
             | (static_cast<uint32_t>(bytes[off + 2]) << 16)
             | (static_cast<uint32_t>(bytes[off + 3]) << 24);
    };
    const int w = static_cast<int>(read4(0));
    const int h = static_cast<int>(read4(4));
    if (w <= 0 || h <= 0) {
        return false;
    }
    const size_t expected_size = 8 + static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (bytes.size() < expected_size) {
        return false;
    }
    *out_width = w;
    *out_height = h;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    out_pixels->resize(count);
    for (size_t i = 0; i < count; ++i) {
        (*out_pixels)[i] = read4(8 + i * 4);
    }
    return true;
#endif
}

}  // namespace XER::Bridge::Skia