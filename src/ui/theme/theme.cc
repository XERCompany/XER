#include "theme.h"

namespace XER::UI::Theme {
namespace {

// shadcn/ui neutral palette (zinc scale). Light variant: white canvas,
// near-black primary, hairline zinc-200 borders, compact radii.
constexpr Tokens kLightTokens{
    .background = {1.0f, 1.0f, 1.0f, 1.0f},          // #ffffff
    .foreground = {0.035f, 0.035f, 0.043f, 1.0f},    // zinc-950 #09090b
    .surface = {1.0f, 1.0f, 1.0f, 1.0f},             // card = white
    .muted = {0.443f, 0.443f, 0.478f, 1.0f},         // zinc-500 #71717a
    .border = {0.894f, 0.894f, 0.906f, 1.0f},        // zinc-200 #e4e4e7
    .accent = {0.094f, 0.094f, 0.106f, 1.0f},        // zinc-900 #18181b primary
    .accent_foreground = {0.98f, 0.98f, 0.98f, 1.0f},// #fafafa
    .focus = {0.631f, 0.631f, 0.667f, 1.0f},         // zinc-400 ring
    .secondary = {0.957f, 0.957f, 0.961f, 1.0f},     // zinc-100 #f4f4f5
    .hover = {0.957f, 0.957f, 0.961f, 1.0f},         // zinc-100 hover fill
    .ring = {0.631f, 0.631f, 0.667f, 1.0f},          // zinc-400
    .danger = {0.718f, 0.11f, 0.11f, 1.0f},          // red-700 #b91c1c
    .danger_foreground = {0.98f, 0.98f, 0.98f, 1.0f},
    .panel = {0.98f, 0.98f, 0.98f, 1.0f},            // #fafafa chrome band
    .input = {1.0f, 1.0f, 1.0f, 1.0f},               // white field
    .pressed = {0.894f, 0.894f, 0.906f, 1.0f},       // zinc-200 press
    .disabled = {0.631f, 0.631f, 0.667f, 1.0f},      // zinc-400 #a1a1aa
    .selection = {0.094f, 0.094f, 0.106f, 0.10f},    // primary wash for rows
    .radius_small = 6.0f,
    .radius_medium = 8.0f,
    .radius_large = 10.0f,
    .spacing_small = 8.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 24.0f,
    .font_small = 12.0f,
    .font_body = 14.0f,
    .font_title = 20.0f,
};

// shadcn/ui dark: near-black zinc-950 canvas, cards one step above,
// hairline zinc-800 borders, white primary, muted zinc-400 text.
// Matches the dark dashboards in the design references.
constexpr Tokens kDarkTokens{
    .background = {0.035f, 0.035f, 0.043f, 1.0f},    // zinc-950 #09090b
    .foreground = {0.98f, 0.98f, 0.98f, 1.0f},       // zinc-50 #fafafa
    .surface = {0.075f, 0.075f, 0.086f, 1.0f},       // #131316 raised card
    .muted = {0.631f, 0.631f, 0.667f, 1.0f},         // zinc-400 #a1a1aa
    .border = {0.153f, 0.153f, 0.165f, 1.0f},        // zinc-800 #27272a hairline
    .accent = {0.98f, 0.98f, 0.98f, 1.0f},           // white primary
    .accent_foreground = {0.094f, 0.094f, 0.106f, 1.0f},// zinc-900 on white
    .focus = {0.827f, 0.827f, 0.847f, 1.0f},         // zinc-300 ring
    .secondary = {0.153f, 0.153f, 0.165f, 1.0f},     // zinc-800 #27272a
    .hover = {0.153f, 0.153f, 0.165f, 1.0f},         // zinc-800 hover fill
    .ring = {0.827f, 0.827f, 0.847f, 1.0f},          // zinc-300
    .danger = {0.498f, 0.106f, 0.106f, 1.0f},        // red-800 #7f1b1b destructive
    .danger_foreground = {0.98f, 0.98f, 0.98f, 1.0f},
    .panel = {0.051f, 0.051f, 0.059f, 1.0f},         // #0d0d0f chrome band
    .input = {0.035f, 0.035f, 0.043f, 1.0f},         // canvas-level sunken field
    .pressed = {0.247f, 0.247f, 0.275f, 1.0f},       // zinc-700 #3f3f46 press
    .disabled = {0.322f, 0.322f, 0.357f, 1.0f},      // zinc-600 #52525b
    .selection = {0.98f, 0.98f, 0.98f, 0.10f},       // white wash for rows
    .radius_small = 6.0f,
    .radius_medium = 8.0f,
    .radius_large = 10.0f,
    .spacing_small = 8.0f,
    .spacing_medium = 16.0f,
    .spacing_large = 24.0f,
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
