// See oklch.h — perceptual color math (Ottosson OKLab/OKLCH reference
// matrices), HSV/HSL helpers, and the shared hex grammar.
#include "oklch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace XER::UI::ColorSpace {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Theme::Color ClampSrgb(Theme::Color color) {
    color.r = std::clamp(color.r, 0.0f, 1.0f);
    color.g = std::clamp(color.g, 0.0f, 1.0f);
    color.b = std::clamp(color.b, 0.0f, 1.0f);
    color.a = std::clamp(color.a, 0.0f, 1.0f);
    return color;
}

bool SrgbInGamut(Theme::Color color) {
    return color.r >= 0.0f && color.r <= 1.0f &&
           color.g >= 0.0f && color.g <= 1.0f &&
           color.b >= 0.0f && color.b <= 1.0f;
}

Oklab OklabFromSrgb(Theme::Color srgb) {
    const float r = SrgbToLinear(std::clamp(srgb.r, 0.0f, 1.0f));
    const float g = SrgbToLinear(std::clamp(srgb.g, 0.0f, 1.0f));
    const float b = SrgbToLinear(std::clamp(srgb.b, 0.0f, 1.0f));

    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);

    Oklab lab;
    lab.l = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    lab.a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    lab.b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
    lab.alpha = srgb.a;
    return lab;
}

Theme::Color SrgbFromOklab(const Oklab& lab) {
    const float l_ = lab.l + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    const float m_ = lab.l - 0.1055613458f * lab.a - 0.0638541729f * lab.b;
    const float s_ = lab.l - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    Theme::Color out;
    out.r = LinearToSrgb(+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    out.g = LinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    out.b = LinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    out.a = lab.alpha;
    return out;
}

Oklch OklchFromSrgb(Theme::Color srgb) {
    const Oklab lab = OklabFromSrgb(srgb);
    Oklch lch;
    lch.l = lab.l;
    lch.c = std::sqrt(lab.a * lab.a + lab.b * lab.b);
    lch.h = WrapHue(std::atan2(lab.b, lab.a) * kRadToDeg);
    lch.a = lab.alpha;
    return lch;
}

Theme::Color SrgbFromOklch(const Oklch& lch) {
    Oklab lab;
    lab.l = lch.l;
    lab.a = lch.c * std::cos(lch.h * kDegToRad);
    lab.b = lch.c * std::sin(lch.h * kDegToRad);
    lab.alpha = lch.a;
    return SrgbFromOklab(lab);
}

Oklch SoftProofToSrgb(Oklch lch) {
    if (lch.c <= 0.0f || SrgbInGamut(SrgbFromOklch(lch))) {
        return lch;
    }
    // Chroma is monotonic in gamut coverage for fixed L/h, so bisect.
    float lo = 0.0f;
    float hi = lch.c;
    for (int i = 0; i < 24; ++i) {
        const float mid = (lo + hi) * 0.5f;
        Oklch probe = lch;
        probe.c = mid;
        if (SrgbInGamut(SrgbFromOklch(probe))) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    lch.c = lo;
    return lch;
}

Hsv HsvFromSrgb(Theme::Color srgb) {
    const float r = std::clamp(srgb.r, 0.0f, 1.0f);
    const float g = std::clamp(srgb.g, 0.0f, 1.0f);
    const float b = std::clamp(srgb.b, 0.0f, 1.0f);
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;

    Hsv hsv;
    hsv.v = mx;
    hsv.s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {
        hsv.h = 0.0f;
    } else if (mx == r) {
        hsv.h = 60.0f * std::fmod((g - b) / d, 6.0f);
    } else if (mx == g) {
        hsv.h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        hsv.h = 60.0f * ((r - g) / d + 4.0f);
    }
    hsv.h = WrapHue(hsv.h);
    hsv.a = srgb.a;
    return hsv;
}

Theme::Color SrgbFromHsv(const Hsv& hsv) {
    const float h = WrapHue(hsv.h) / 60.0f;
    const float s = std::clamp(hsv.s, 0.0f, 1.0f);
    const float v = std::clamp(hsv.v, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    const int sector = static_cast<int>(h) % 6;
    switch (sector) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    case 5: r = c; b = x; break;
    default: break;
    }
    return {r + m, g + m, b + m, hsv.a};
}

Hsl HslFromSrgb(Theme::Color srgb) {
    const float r = std::clamp(srgb.r, 0.0f, 1.0f);
    const float g = std::clamp(srgb.g, 0.0f, 1.0f);
    const float b = std::clamp(srgb.b, 0.0f, 1.0f);
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;

    Hsl hsl;
    hsl.l = (mx + mn) * 0.5f;
    if (d <= 0.0f) {
        hsl.h = 0.0f;
        hsl.s = 0.0f;
    } else {
        hsl.s = d / (1.0f - std::fabs(2.0f * hsl.l - 1.0f));
        if (mx == r) {
            hsl.h = 60.0f * std::fmod((g - b) / d, 6.0f);
        } else if (mx == g) {
            hsl.h = 60.0f * ((b - r) / d + 2.0f);
        } else {
            hsl.h = 60.0f * ((r - g) / d + 4.0f);
        }
        hsl.h = WrapHue(hsl.h);
    }
    hsl.a = srgb.a;
    return hsl;
}

Theme::Color SrgbFromHsl(const Hsl& hsl) {
    const float h = WrapHue(hsl.h) / 60.0f;
    const float s = std::clamp(hsl.s, 0.0f, 1.0f);
    const float l = std::clamp(hsl.l, 0.0f, 1.0f);
    const float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = l - c * 0.5f;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    const int sector = static_cast<int>(h) % 6;
    switch (sector) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    case 5: r = c; b = x; break;
    default: break;
    }
    return {r + m, g + m, b + m, hsl.a};
}

Theme::Color ColorFromHex(std::string_view text) {
    Theme::Color out{0.0f, 0.0f, 0.0f, 1.0f};
    std::string_view s = text;
    if (!s.empty() && s.front() == '#') {
        s.remove_prefix(1);
    }
    if (s.size() != 6 && s.size() != 8) {
        return out;
    }
    std::uint8_t ch[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < s.size() / 2; ++i) {
        const int hi = HexDigit(s[i * 2]);
        const int lo = HexDigit(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return {0.0f, 0.0f, 0.0f, 1.0f};
        }
        ch[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    out.r = ch[0] / 255.0f;
    out.g = ch[1] / 255.0f;
    out.b = ch[2] / 255.0f;
    out.a = ch[3] / 255.0f;
    return out;
}

std::string HexFromColor(Theme::Color color, bool include_alpha) {
    const Theme::Color c = ClampSrgb(color);
    char buf[10];
    if (include_alpha) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
                      static_cast<int>(c.r * 255.0f + 0.5f),
                      static_cast<int>(c.g * 255.0f + 0.5f),
                      static_cast<int>(c.b * 255.0f + 0.5f),
                      static_cast<int>(c.a * 255.0f + 0.5f));
    } else {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                      static_cast<int>(c.r * 255.0f + 0.5f),
                      static_cast<int>(c.g * 255.0f + 0.5f),
                      static_cast<int>(c.b * 255.0f + 0.5f));
    }
    return buf;
}

float WrapHue(float degrees) {
    float h = std::fmod(degrees, 360.0f);
    if (h < 0.0f) {
        h += 360.0f;
    }
    return h;
}

float RelativeLuminance(Theme::Color srgb) {
    return 0.2126f * SrgbToLinear(std::clamp(srgb.r, 0.0f, 1.0f)) +
           0.7152f * SrgbToLinear(std::clamp(srgb.g, 0.0f, 1.0f)) +
           0.0722f * SrgbToLinear(std::clamp(srgb.b, 0.0f, 1.0f));
}

float ContrastRatio(Theme::Color a, Theme::Color b) {
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

} // namespace XER::UI::ColorSpace
