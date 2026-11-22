#ifndef XER_UI_COLORSPACE_OKLCH_H
#define XER_UI_COLORSPACE_OKLCH_H

// Perceptual color math for the widget layer.
//
// OKLCH (Ottosson, 2020) is the canonical state space for the color-picking
// widgets: lightness is perceptually uniform, so slider positions stop lying
// the way HSL lightness does. All conversions here operate on Theme::Color
// (linear-light floats are NOT used on the wire — Theme::Color channels are
// gamma-encoded sRGB in [0,1]) plus small POD structs for the picker modes.
//
// Conventions:
//   * hue is degrees in [0,360); chroma is unbounded (sRGB gamut peaks ~0.37)
//   * alpha rides along untouched in every conversion
//   * out-of-gamut OKLCH -> sRGB returns unclamped floats; callers clamp or
//     soft-proof (SrgbInGamut / SoftProofToSrgb) as the UX requires

#include <string>
#include <string_view>

#include "theme/color.h"

namespace XER::UI::ColorSpace {

struct Oklch {
    float l = 0.0f;   // perceived lightness, ~0..1
    float c = 0.0f;   // chroma, >= 0 (unbounded)
    float h = 0.0f;   // hue degrees [0,360); meaningless when c ~ 0
    float a = 1.0f;
};

struct Oklab {
    float l = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
    float alpha = 1.0f;
};

// h in degrees [0,360); s/v/l in [0,1].
struct Hsv { float h = 0.0f, s = 0.0f, v = 0.0f, a = 1.0f; };
struct Hsl { float h = 0.0f, s = 0.0f, l = 0.0f, a = 1.0f; };

// ── sRGB transfer ─────────────────────────────────────────────────────────
float SrgbToLinear(float c);
float LinearToSrgb(float c);

// Clamps r/g/b into [0,1] (alpha clamped too). No-op for in-gamut colors.
Theme::Color ClampSrgb(Theme::Color color);
// True when r/g/b all sit inside [0,1] (alpha ignored).
bool SrgbInGamut(Theme::Color color);

// ── OKLab / OKLCH ─────────────────────────────────────────────────────────
// Input/output is gamma-encoded sRGB (Theme::Color contract); the linear
// stage is applied internally.
Oklab OklabFromSrgb(Theme::Color srgb);
Theme::Color SrgbFromOklab(const Oklab& lab);   // unclamped

Oklch OklchFromSrgb(Theme::Color srgb);
Theme::Color SrgbFromOklch(const Oklch& lch);   // unclamped

// Chroma-reducing gamut map (Amplo-style soft proof): binary-searches c
// down until the color lands in the sRGB gamut, keeping L and h so the hue
// stays true instead of per-channel clipping. In-gamut input returns
// unchanged.
Oklch SoftProofToSrgb(Oklch lch);

// ── HSV / HSL ─────────────────────────────────────────────────────────────
Hsv HsvFromSrgb(Theme::Color srgb);
Theme::Color SrgbFromHsv(const Hsv& hsv);
Hsl HslFromSrgb(Theme::Color srgb);
Theme::Color SrgbFromHsl(const Hsl& hsl);

// ── Hex grammar ───────────────────────────────────────────────────────────
// "#RRGGBB" / "#RRGGBBAA" with optional '#', case-insensitive — the same
// grammar the Lab eyedropper helpers (tools/video_editor/lab/color_pick.h)
// and the matte parser accept. Malformed input yields opaque black.
Theme::Color ColorFromHex(std::string_view text);
// Canonical "#RRGGBB" (include_alpha=false) or "#RRGGBBAA" uppercase form.
std::string HexFromColor(Theme::Color color, bool include_alpha = false);

// ── Misc ──────────────────────────────────────────────────────────────────
// Wraps degrees into [0,360).
float WrapHue(float degrees);
// WCAG relative luminance of a gamma-encoded sRGB color (0..1).
float RelativeLuminance(Theme::Color srgb);
// WCAG contrast ratio 1..21 between two opaque sRGB colors.
float ContrastRatio(Theme::Color a, Theme::Color b);

} // namespace XER::UI::ColorSpace

#endif // XER_UI_COLORSPACE_OKLCH_H
