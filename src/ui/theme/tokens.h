#ifndef XER_UI_THEME_TOKENS_H
#define XER_UI_THEME_TOKENS_H

#include "color.h"

namespace XER::UI::Theme {

struct Tokens {
    Color background;
    Color foreground;
    Color surface;
    Color muted;
    Color border;
    Color accent;
    // Text/icon color drawn on top of `accent` fills.
    Color accent_foreground;
    Color focus;
    // Subtle filled backgrounds (slider/progress tracks, secondary badge).
    Color secondary;
    // Background shown behind hovered interactive controls.
    Color hover;
    // Outer ring drawn around the focused control (shadcn "ring").
    Color ring;
    // Destructive actions (shadcn "destructive"): filled danger buttons,
    // danger menu items, error accents.
    Color danger;
    // Text/icon color drawn on top of `danger` fills.
    Color danger_foreground;
    // Panel chrome: app chrome surfaces (title bar, sidebars, docks) that
    // sit between `background` and `surface` in elevation.
    Color panel{};
    // Recessed field background (text inputs, search bars, combo fields).
    // Slightly darker than `surface` in dark themes so fields read sunken.
    Color input{};
    // Fill shown while an interactive control is held down (one step
    // stronger than `hover`).
    Color pressed{};
    // Text/icon color for disabled controls (dimmer than `muted`).
    Color disabled{};
    // Subtle selection highlight for list/table rows — `accent` at low
    // alpha so the foreground text stays readable on top.
    Color selection{};
    float radius_small;
    float radius_medium;
    float radius_large;
    float spacing_small;
    float spacing_medium;
    float spacing_large;
    float font_small;
    float font_body;
    float font_title;
    // ---- media-editor semantic colors (NLE chrome) ----
    // Appended for the video_editor redesign; the stock light/dark
    // themes default them to sensible accent-family hues so generic
    // consumers see reasonable colors without opting in.
    // Bright readout digits — timecode counters, meter values, numeric
    // columns (pair with TextStyle::monospace for the digits).
    Color timecode{};
    // Waveform traces on clips and audio-overview panels.
    Color waveform{};
    // Per-kind track chrome: header kind-bars, track-color tints
    // (video reads blue, audio reads teal).
    Color track_video{};
    Color track_audio{};
    // Playhead / current-time indicator line over ruler + timeline.
    Color playhead{};
    // Small rounded "fx"-style chip drawn on clips carrying applied
    // effects or warnings.
    Color warning_chip{};
    // Stoplight status trio — render-status bars on the timeline
    // (Premiere's green/yellow/red needs-render strip) and the
    // monitor's dropped-frame indicator.
    Color status_ok{};
    Color status_warn{};
    Color status_bad{};
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_TOKENS_H
