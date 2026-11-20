#include "ve_theme.h"

#include "ui/core/window.h"

namespace XER::VideoEditor::Ui::VeTheme {

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
    // Refined neutral-dark NLE palette: a three-step elevation ramp
    // (background < panel < surface), hairline borders, and the brand
    // violet accent (assets/editor.yaml `Accent:`) replacing the stock
    // monochrome primary so interactive state actually reads.
    const Color accent =
        accent_argb != 0u ? FromArgb(accent_argb)
                          : FromRgb(kDefaultAccentRgb);
    Tokens t{};
    t.background = {0.102f, 0.114f, 0.137f, 1.0f};   // #1a1d23 canvas
    t.foreground = {0.902f, 0.910f, 0.925f, 1.0f};   // #e6e8ec
    t.surface = {0.133f, 0.149f, 0.180f, 1.0f};      // #22262e panels
    t.muted = {0.604f, 0.627f, 0.671f, 1.0f};        // #9aa0ab dim text
    t.border = {0.188f, 0.212f, 0.247f, 1.0f};       // #30363f hairline
    t.accent = accent;
    t.accent_foreground = {0.965f, 0.957f, 1.0f, 1.0f};  // on-accent text
    t.focus = accent;
    t.secondary = {0.165f, 0.184f, 0.220f, 1.0f};    // #2a2f38 quiet fill
    t.hover = {0.184f, 0.204f, 0.243f, 1.0f};        // #2f3440 hover
    t.ring = WithAlpha(accent, 0.85f);
    t.danger = {0.898f, 0.282f, 0.302f, 1.0f};       // #e5484d
    t.danger_foreground = {1.0f, 0.98f, 0.98f, 1.0f};
    t.panel = {0.118f, 0.133f, 0.161f, 1.0f};        // #1e2229 chrome
    t.input = {0.086f, 0.098f, 0.118f, 1.0f};        // #16191e sunken
    t.pressed = {0.224f, 0.247f, 0.290f, 1.0f};      // #393f4a
    t.disabled = {0.361f, 0.388f, 0.439f, 1.0f};     // #5c6370
    t.selection = WithAlpha(accent, 0.22f);          // accent row wash
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

}  // namespace XER::VideoEditor::Ui::VeTheme
