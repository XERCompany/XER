#include "ve_theme.h"

#include "ui/core/window.h"

namespace XER::VideoEditor::Ui2::Theme {

Color Mix(Color a, Color b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color FromRgb(std::uint32_t rrggbb) {
    return Color{static_cast<float>((rrggbb >> 16) & 0xFFu) / 255.0f,
                 static_cast<float>((rrggbb >> 8) & 0xFFu) / 255.0f,
                 static_cast<float>(rrggbb & 0xFFu) / 255.0f, 1.0f};
}

Color FromArgb(std::uint32_t aarrggbb) {
    return Color{static_cast<float>((aarrggbb >> 16) & 0xFFu) / 255.0f,
                 static_cast<float>((aarrggbb >> 8) & 0xFFu) / 255.0f,
                 static_cast<float>(aarrggbb & 0xFFu) / 255.0f,
                 static_cast<float>((aarrggbb >> 24) & 0xFFu) / 255.0f};
}

Color ClipBaseColor(bool video_track, std::size_t track_index) {
    // Alternate two close hues per kind so stacked same-kind tracks
    // don't merge visually.
    const bool alt = (track_index % 2u) != 0u;
    if (video_track) {
        return alt ? kClipVideoAlt : kClipVideo;
    }
    return alt ? kClipAudioAlt : kClipAudio;
}

Color TrackKindColor(bool video_track) {
    return video_track ? kClipVideo : kClipAudio;
}

Tokens DarkTokens(std::uint32_t accent_argb) {
    // Reference modern-NLE palette: near-black cool-neutral surfaces
    // (#141417 canvas → #1a1a20 chrome → #222229 raised cards), subtle
    // hairline borders, medium radii, and a cyan/teal accent family.
    // assets/editor.yaml `Accent:` still overrides the interactive
    // accent; the NLE semantic colors below stay fixed.
    const Color accent =
        accent_argb != 0u ? FromArgb(accent_argb)
                          : FromRgb(kDefaultAccentRgb);
    Tokens t{};
    t.background = {0.078f, 0.078f, 0.090f, 1.0f};   // #141417 canvas
    t.foreground = {0.910f, 0.918f, 0.933f, 1.0f};   // #e8eaee
    t.surface = {0.133f, 0.133f, 0.161f, 1.0f};      // #222229 cards
    t.muted = {0.561f, 0.573f, 0.620f, 1.0f};        // #8f929e dim text
    t.border = {0.176f, 0.176f, 0.208f, 1.0f};       // #2d2d35 hairline
    t.accent = accent;
    // Near-black on the bright cyan accent (accent fills are vivid).
    t.accent_foreground = {0.043f, 0.055f, 0.071f, 1.0f};  // #0b0e12
    t.focus = accent;
    t.secondary = {0.149f, 0.149f, 0.180f, 1.0f};    // #26262e quiet fill
    t.hover = {0.184f, 0.184f, 0.220f, 1.0f};        // #2f2f38 hover
    t.ring = WithAlpha(accent, 0.85f);
    t.danger = {0.898f, 0.282f, 0.302f, 1.0f};       // #e5484d
    t.danger_foreground = {1.0f, 0.98f, 0.98f, 1.0f};
    t.panel = {0.102f, 0.102f, 0.125f, 1.0f};        // #1a1a20 chrome
    t.input = {0.055f, 0.055f, 0.071f, 1.0f};        // #0e0e12 sunken
    t.pressed = {0.227f, 0.227f, 0.271f, 1.0f};      // #3a3a45 press
    t.disabled = {0.341f, 0.353f, 0.396f, 1.0f};     // #575a65
    t.selection = WithAlpha(accent, 0.20f);          // cyan row wash
    // NLE semantic colors (tokens.h tail) — fixed cyan/teal family,
    // independent of the configurable accent.
    t.timecode = kTimecode;
    t.waveform = kWaveform;
    t.track_video = kTrackVideo;
    t.track_audio = kTrackAudio;
    t.playhead = kPlayhead;
    t.warning_chip = kWarningChip;
    t.radius_small = kRadiusSm;
    t.radius_medium = kRadiusMd;
    t.radius_large = kRadiusLg;
    t.spacing_small = kSpaceSm;
    t.spacing_medium = kSpaceMd;
    t.spacing_large = kSpaceXl;
    t.font_small = kFontCaption;
    t.font_body = kFontBody;
    t.font_title = kFontTitle;
    return t;
}

void ApplyToWindow(XER::UI::Core::Window* window,
                   std::uint32_t accent_argb) {
    if (window == nullptr) return;
    window->SetTokens(DarkTokens(accent_argb));
}

}  // namespace XER::VideoEditor::Ui2::Theme
