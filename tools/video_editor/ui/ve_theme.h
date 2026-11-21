// XER Video Editor design system — the single source of truth for the
// app's visual language. Two layers:
//
//   1. Tokens(): a full Theme::Tokens set (the refined dark palette,
//      accent, spacing/radius/font ramps) applied to the window in
//      main.cc so every tokens.* read across the UI picks it up.
//   2. Named constants below for the metrics/semantic colors the shared
//      token struct doesn't cover (timeline clip colors, header tints,
//      text insets, control heights). Widgets must use these instead of
//      scattered literals.
//
// Grid: 4px base unit. Everything snaps to 4/8/12/16/24.
#pragma once

#include <cstddef>
#include <cstdint>

#include "theme/color.h"
#include "theme/tokens.h"

namespace XER::UI::Core {
class Window;
}

namespace XER::VideoEditor::Ui::VeTheme {

using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;

// ---- spacing scale (4px grid) ----
inline constexpr float kSpaceXxs = 2.0f;    // hairline nudges
inline constexpr float kSpaceXs = 4.0f;     // tight inner gaps
inline constexpr float kSpaceSm = 8.0f;     // default gap between controls
inline constexpr float kSpaceMd = 12.0f;    // panel content padding
inline constexpr float kSpaceLg = 16.0f;    // section separation
inline constexpr float kSpaceXl = 24.0f;    // major band separation

// ---- corner radius scale ----
inline constexpr float kRadiusSm = 4.0f;    // chips, clips, small fills
inline constexpr float kRadiusMd = 6.0f;    // buttons, inputs, thumbs
inline constexpr float kRadiusLg = 8.0f;    // panels, cards, dialogs

// ---- text metrics ----
// Text never touches a control edge: every label gets this inset.
inline constexpr float kTextInsetX = 10.0f;
inline constexpr float kTextInsetY = 6.0f;
inline constexpr float kFontCaption = 11.0f;
inline constexpr float kFontBody = 13.0f;
inline constexpr float kFontTitle = 18.0f;

// ---- control metrics ----
inline constexpr float kRowHeight = 24.0f;       // list/inspector rows
inline constexpr float kControlHeight = 28.0f;   // inputs, small buttons
inline constexpr float kPanelHeaderHeight = 30.0f;  // panel title band

// ---- overlay colors ----
// Modal backdrop dim: dialogs, sheets and the shortcuts overlay all
// darken the workspace under their card with exactly this scrim.
inline constexpr Color kScrim{0.0f, 0.0f, 0.0f, 0.58f};
// The single canonical "overlay black" for chips/badges drawn on top
// of media (monitor overlays, preview badges) — panels must use this
// instead of hand-rolled {0,0,0,a} literals.
inline constexpr Color kOverlayChip{0.0f, 0.0f, 0.0f, 0.62f};

// ---- semantic timeline colors ----
// Kind-coded clip fills: video reads blue, audio reads green at a
// glance (Premiere/CapCut convention), with per-track hue rotation so
// neighbouring tracks of the same kind stay distinguishable.
Color ClipBaseColor(bool video_track, std::size_t track_index);
// Tint used for a track header's left kind-bar and row wash.
Color TrackKindColor(bool video_track);

inline constexpr Color kClipVideo{0.24f, 0.50f, 0.82f, 1.0f};
inline constexpr Color kClipVideoAlt{0.38f, 0.42f, 0.78f, 1.0f};
inline constexpr Color kClipAudio{0.20f, 0.62f, 0.42f, 1.0f};
inline constexpr Color kClipAudioAlt{0.16f, 0.56f, 0.55f, 1.0f};

// Utility: multiply-alpha (draw helpers everywhere need it).
inline Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Linear blend a→b by t in float space.
Color Mix(Color a, Color b, float t);

// "#rrggbb" / "#aarrggbb" packed int → float color (editor.yaml accent).
Color FromRgb(std::uint32_t rrggbb);
Color FromArgb(std::uint32_t aarrggbb);

// Full token set for the editor's dark UI. `accent_argb` is the
// assets/editor.yaml Accent value (0 → the compiled brand default).
Tokens DarkTokens(std::uint32_t accent_argb = 0);

// The brand accent used when assets declare none (#8B5CF6 violet).
inline constexpr std::uint32_t kDefaultAccentRgb = 0x8B5CF6u;

// Builds DarkTokens(accent_argb) and installs them on the window via
// Window::SetTokens. Call after Window::SetTheme(Variant::Dark).
void ApplyToWindow(XER::UI::Core::Window* window,
                   std::uint32_t accent_argb = 0);

}  // namespace XER::VideoEditor::Ui::VeTheme
