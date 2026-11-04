#include "theme.h"

namespace XER::UI::Theme {
namespace {

constexpr Tokens kLightTokens{
    .background = {0.97f, 0.97f, 0.98f, 1.0f},
    .foreground = {0.10f, 0.11f, 0.13f, 1.0f},
    .surface = {1.0f, 1.0f, 1.0f, 1.0f},
    .muted = {0.42f, 0.45f, 0.50f, 1.0f},
    .border = {0.82f, 0.84f, 0.87f, 1.0f},
    .accent = {0.12f, 0.36f, 0.78f, 1.0f},
    .focus = {0.08f, 0.42f, 0.96f, 1.0f},
    .radius_small = 3.0f,
    .radius_medium = 6.0f,
    .spacing_small = 4.0f,
    .spacing_medium = 8.0f,
    .spacing_large = 16.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 20.0f,
};

constexpr Tokens kDarkTokens{
    .background = {0.08f, 0.09f, 0.11f, 1.0f},
    .foreground = {0.91f, 0.92f, 0.94f, 1.0f},
    .surface = {0.13f, 0.14f, 0.17f, 1.0f},
    .muted = {0.62f, 0.65f, 0.70f, 1.0f},
    .border = {0.29f, 0.31f, 0.35f, 1.0f},
    .accent = {0.35f, 0.60f, 0.96f, 1.0f},
    .focus = {0.45f, 0.68f, 1.0f, 1.0f},
    .radius_small = 3.0f,
    .radius_medium = 6.0f,
    .spacing_small = 4.0f,
    .spacing_medium = 8.0f,
    .spacing_large = 16.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 20.0f,
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
