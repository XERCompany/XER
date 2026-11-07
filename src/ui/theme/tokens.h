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
