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
// Reference NLE chrome uses a medium radius on floating panels
// (8-10px) with smaller rounding on clips/chips.
inline constexpr float kRadiusSm = 4.0f;    // chips, clips, small fills
inline constexpr float kRadiusMd = 7.0f;    // buttons, inputs, thumbs
inline constexpr float kRadiusLg = 10.0f;   // panels, cards, dialogs

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
// Kind-coded clip fills: video reads blue, audio reads teal at a
// glance (Premiere/CapCut convention), with per-track hue rotation so
// neighbouring tracks of the same kind stay distinguishable.
Color ClipBaseColor(bool video_track, std::size_t track_index);
// Tint used for a track header's left kind-bar and row wash.
Color TrackKindColor(bool video_track);

inline constexpr Color kClipVideo{0.24f, 0.51f, 0.90f, 1.0f};
inline constexpr Color kClipVideoAlt{0.37f, 0.43f, 0.87f, 1.0f};
inline constexpr Color kClipAudio{0.13f, 0.66f, 0.52f, 1.0f};
inline constexpr Color kClipAudioAlt{0.12f, 0.58f, 0.60f, 1.0f};

// ---- NLE accent palette (cyan/teal scheme) ----
// These mirror the matching Theme::Tokens fields DarkTokens() writes —
// panels may use either the token (tokens.timecode etc.) or these
// constants; keep them in sync if the palette moves.
// Bright cyan readout digits: timecode counters, meter values, any
// numeric readout — pair with Rendering::TextStyle monospace=true.
inline constexpr Color kTimecode{0.133f, 0.827f, 0.933f, 1.0f};   // #22d3ee
// Teal-green waveform traces and audio meter color.
inline constexpr Color kWaveform{0.176f, 0.831f, 0.627f, 1.0f};   // #2dd4a0
// Track chrome kind colors: header kind-bar, mute/solo/lock tints,
// track-color wells (video = blue, audio = teal).
inline constexpr Color kTrackVideo{0.231f, 0.510f, 0.965f, 1.0f}; // #3b82f6
inline constexpr Color kTrackAudio{0.176f, 0.831f, 0.749f, 1.0f}; // #2dd4bf
// Playhead line and current-time indicator (reads over clip fills).
inline constexpr Color kPlayhead{0.216f, 0.843f, 0.941f, 1.0f};   // #37d7f0
// Small amber "fx" chip drawn on clips carrying applied effects.
inline constexpr Color kWarningChip{0.984f, 0.749f, 0.141f, 1.0f};// #fbbf24
// Text/glyph drawn on top of kWarningChip fills (near-black amber-950).
inline constexpr Color kWarningChipText{0.110f, 0.086f, 0.016f, 1.0f};

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

// The brand accent used when assets declare none (cyan #22D3EE — the
// reference NLE accent; assets/editor.yaml `Accent:` overrides it).
inline constexpr std::uint32_t kDefaultAccentRgb = 0x22D3EEu;

// Builds DarkTokens(accent_argb) and installs them on the window via
// Window::SetTokens. Call after Window::SetTheme(Variant::Dark).
void ApplyToWindow(XER::UI::Core::Window* window,
                   std::uint32_t accent_argb = 0);

}  // namespace XER::VideoEditor::Ui::VeTheme
