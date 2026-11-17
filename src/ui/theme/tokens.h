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
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_TOKENS_H
