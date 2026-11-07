#include "theme.h"

namespace XER::UI::Theme {
namespace {

// Neutral theme based on ui-widget-preview.html (A · Neutral)
constexpr Tokens kLightTokens{
    .background = {0.96f, 0.96f, 0.96f, 1.0f}, // #f5f5f5
    .foreground = {0.09f, 0.09f, 0.09f, 1.0f}, // #171717
    .surface = {1.0f, 1.0f, 1.0f, 1.0f},      // #ffffff
    .muted = {0.45f, 0.45f, 0.45f, 1.0f},     // #737373
    .border = {0.87f, 0.87f, 0.87f, 1.0f},     // #dedede
    .accent = {0.09f, 0.40f, 0.20f, 1.0f},    // #166534 (green accent)
    .focus = {0.09f, 0.09f, 0.11f, 1.0f},     // #18181b
    .radius_small = 6.0f,
    .radius_medium = 9.0f,
    .spacing_small = 9.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 20.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 30.0f,
};

// Midnight theme based on ui-widget-preview.html (B · Midnight)
constexpr Tokens kDarkTokens{
    .background = {0.06f, 0.07f, 0.09f, 1.0f}, // #111318
    .foreground = {0.96f, 0.96f, 0.96f, 1.0f}, // #f4f4f5
    .surface = {0.12f, 0.13f, 0.15f, 1.0f},    // #202127
    .muted = {0.63f, 0.63f, 0.66f, 1.0f},     // #a1a1aa
    .border = {0.22f, 0.23f, 0.27f, 1.0f},     // #393b44
    .accent = {0.06f, 0.45f, 0.14f, 1.0f},    // #0f6b24 (green accent)
    .focus = {0.96f, 0.96f, 0.96f, 1.0f},     // #f4f4f5
    .radius_small = 6.0f,
    .radius_medium = 9.0f,
    .spacing_small = 9.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 20.0f,
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
