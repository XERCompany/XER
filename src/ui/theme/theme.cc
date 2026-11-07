#include "theme.h"

namespace XER::UI::Theme {
namespace {

// Headless UI / Tailwind-style neutral theme: zinc neutrals, indigo accent,
// Tailwind radii (rounded-md/lg/xl ≈ 6/8/12px) and accent focus rings.
constexpr Tokens kLightTokens{
    .background = {0.945f, 0.945f, 0.955f, 1.0f},   // zinc-100 #f4f4f6 canvas
    .foreground = {0.09f, 0.09f, 0.11f, 1.0f},   // zinc-900 #18181b
    .surface = {1.0f, 1.0f, 1.0f, 1.0f},         // white cards pop on canvas
    .muted = {0.33f, 0.33f, 0.37f, 1.0f},        // zinc-600 #52525b readable
    .border = {0.78f, 0.78f, 0.82f, 1.0f},       // zinc-300 #c7c7d1 visible
    .accent = {0.31f, 0.27f, 0.90f, 1.0f},       // indigo-600 #4f46e5
    .accent_foreground = {1.0f, 1.0f, 1.0f, 1.0f},
    .focus = {0.09f, 0.09f, 0.11f, 1.0f},        // zinc-900
    .secondary = {0.90f, 0.90f, 0.92f, 1.0f},    // zinc-200 active chip
    .hover = {0.87f, 0.87f, 0.90f, 1.0f},        // zinc-200 hover
    .ring = {0.31f, 0.27f, 0.90f, 1.0f},         // indigo-600 focus ring
    .danger = {0.86f, 0.15f, 0.15f, 1.0f},       // red-600 #dc2626
    .danger_foreground = {1.0f, 1.0f, 1.0f, 1.0f},
    .radius_small = 6.0f,
    .radius_medium = 8.0f,
    .radius_large = 12.0f,
    .spacing_small = 8.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 24.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 30.0f,
};

// Dark "studio" variant: deep blue-charcoal base (Blender/DAW feel),
// cool zinc surfaces with a blue cast, vivid indigo accent that stays
// legible on near-black.
constexpr Tokens kDarkTokens{
    .background = {0.043f, 0.055f, 0.078f, 1.0f},   // #0b0e14 deep blue-black
    .foreground = {0.91f, 0.93f, 0.96f, 1.0f},      // #e8ecf4 cool white
    .surface = {0.095f, 0.12f, 0.16f, 1.0f},       // #18202a card, more present
    .muted = {0.60f, 0.65f, 0.72f, 1.0f},           // #99a6b8 readable slate
    .border = {0.19f, 0.23f, 0.30f, 1.0f},          // #313b4d visible hairline
    .accent = {0.51f, 0.55f, 0.97f, 1.0f},          // #828cf7 indigo-400
    .accent_foreground = {0.07f, 0.07f, 0.10f, 1.0f},// dark text on bright accent
    .focus = {0.51f, 0.55f, 0.97f, 1.0f},
    .secondary = {0.14f, 0.175f, 0.235f, 1.0f},     // #242d3c lifted chip
    .hover = {0.17f, 0.21f, 0.28f, 1.0f},           // #2b3647 hover
    .ring = {0.51f, 0.55f, 0.97f, 1.0f},
    .danger = {0.94f, 0.30f, 0.30f, 1.0f},          // #f04d4d
    .danger_foreground = {1.0f, 1.0f, 1.0f, 1.0f},
    .radius_small = 6.0f,
    .radius_medium = 8.0f,
    .radius_large = 12.0f,
    .spacing_small = 8.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 24.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 30.0f,
};

} // namespace

void Provider::SetVariant(Variant variant) noexcept {
    variant_ = variant;
}

Variant Provider::GetVariant() const noexcept {
    return variant_;
}

const Tokens& Provider::GetTokens() const noexcept {
    return variant_ == Variant::Dark ? kDarkTokens : kLightTokens;
}

} // namespace XER::UI::Theme
