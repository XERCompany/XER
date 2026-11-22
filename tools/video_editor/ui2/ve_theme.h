// ui2 design system — port of the ui/ve_theme.h constants onto
// the shared Theme::Tokens NLE fields (spec §7). The palette is the
// same verified ramp the old shell ran:
//
//   #141417 canvas · #1a1a20 panel chrome · #222229 raised cards ·
//   #0e0e12 sunken inputs · #2d2d35 hairline borders · #22D3EE accent
//
// plus the fixed cyan/teal NLE semantic family (timecode digits, teal
// waveforms, blue/teal track chrome, amber fx chips). Clip-type colors
// are carried as constants + a ClipTypeColor helper so M2 surfaces can
// kind-code clips exactly like the old timeline widget did.
//
// Grid: 4px base unit. Everything snaps to 4/8/12/16/24.
#pragma once

#include <cstdint>
#include <cstddef>

#include "theme/color.h"
#include "theme/tokens.h"

namespace XER::UI::Core {
class Window;
}

namespace XER::VideoEditor::Ui2::Theme {

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
inline constexpr float kRadiusMd = 7.0f;    // buttons, inputs, thumbs
inline constexpr float kRadiusLg = 10.0f;   // panels, cards, dialogs

// ---- text metrics ----
inline constexpr float kTextInsetX = 10.0f;
inline constexpr float kTextInsetY = 6.0f;
inline constexpr float kFontCaption = 11.0f;
inline constexpr float kFontBody = 13.0f;
inline constexpr float kFontTitle = 18.0f;

// ---- control metrics (pro NLE density) ----
inline constexpr float kRowHeight = 24.0f;        // list/inspector rows
inline constexpr float kControlHeight = 28.0f;    // inputs, small buttons
inline constexpr float kPanelHeaderHeight = 30.0f;  // panel title band
inline constexpr float kTitleBarHeight = 38.0f;   // AppTitleBar band
inline constexpr float kNavRailWidth = 46.0f;     // far-left icon rail
inline constexpr float kStatusStripHeight = 26.0f;
inline constexpr float kDockTabStripHeight = 30.0f;
inline constexpr float kToolStripHeight = 30.0f;  // timeline tool strip

// ---- overlay colors ----
// Modal backdrop dim: dialogs, sheets and the shortcuts overlay all
// darken the workspace under their card with exactly this scrim.
inline constexpr Color kScrim{0.0f, 0.0f, 0.0f, 0.58f};
// Canonical "overlay black" for chips/badges drawn on top of media.
inline constexpr Color kOverlayChip{0.0f, 0.0f, 0.0f, 0.62f};

// ---- semantic timeline colors ----
// Kind-coded clip fills (Premiere/CapCut convention), with per-track hue
// alternation so neighbouring same-kind tracks stay distinguishable.
Color ClipBaseColor(bool video_track, std::size_t track_index);
// Tint used for a track header's left kind-bar and row wash.
Color TrackKindColor(bool video_track);

inline constexpr Color kClipVideo{0.24f, 0.51f, 0.90f, 1.0f};
inline constexpr Color kClipVideoAlt{0.37f, 0.43f, 0.87f, 1.0f};
inline constexpr Color kClipAudio{0.13f, 0.66f, 0.52f, 1.0f};
inline constexpr Color kClipAudioAlt{0.12f, 0.58f, 0.60f, 1.0f};

// Non-media clip-type accents (CapCut-style type coding on video lanes):
// titles read violet, generated sources slate, adjustment layers steel.
inline constexpr Color kClipTitle{0.60f, 0.40f, 0.86f, 1.0f};      // violet
inline constexpr Color kClipGenerated{0.42f, 0.50f, 0.66f, 1.0f};  // slate
inline constexpr Color kClipAdjustment{0.55f, 0.63f, 0.75f, 1.0f}; // steel

// ---- NLE accent palette (cyan/teal scheme) ----
// Mirror of the Theme::Tokens NLE fields DarkTokens() writes —
// surfaces may read either the token (tokens.timecode etc.) or these
// constants; keep them in sync if the palette moves.
inline constexpr Color kTimecode{0.133f, 0.827f, 0.933f, 1.0f};   // #22d3ee
inline constexpr Color kWaveform{0.176f, 0.831f, 0.627f, 1.0f};   // #2dd4a0
inline constexpr Color kTrackVideo{0.231f, 0.510f, 0.965f, 1.0f}; // #3b82f6
inline constexpr Color kTrackAudio{0.176f, 0.831f, 0.749f, 1.0f}; // #2dd4bf
inline constexpr Color kPlayhead{0.216f, 0.843f, 0.941f, 1.0f};   // #37d7f0
inline constexpr Color kWarningChip{0.984f, 0.749f, 0.141f, 1.0f};// #fbbf24
inline constexpr Color kWarningChipText{0.110f, 0.086f, 0.016f, 1.0f};

// Utility: multiply-alpha.
inline Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Linear blend a→b by t in float space.
Color Mix(Color a, Color b, float t);

// "#rrggbb" / "#aarrggbb" packed int → float color.
Color FromRgb(std::uint32_t rrggbb);
Color FromArgb(std::uint32_t aarrggbb);

// Full token set for the editor's dark UI. `accent_argb` is the
// assets/editor.yaml Accent value (0 → the compiled brand default).
Tokens DarkTokens(std::uint32_t accent_argb = 0);

// The brand accent used when assets declare none (cyan #22D3EE —
// assets/editor.yaml `Accent:` overrides it).
inline constexpr std::uint32_t kDefaultAccentRgb = 0x22D3EEu;

// Builds DarkTokens(accent_argb) and installs them on the window via
// Window::SetTokens. Call after Window::SetTheme(Variant::Dark).
void ApplyToWindow(XER::UI::Core::Window* window,
                   std::uint32_t accent_argb = 0);

}  // namespace XER::VideoEditor::Ui2::Theme
