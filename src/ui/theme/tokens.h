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
    Color focus;
    float radius_small;
    float radius_medium;
    float spacing_small;
    float spacing_medium;
    float spacing_large;
    float font_small;
    float font_body;
    float font_title;
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_TOKENS_H
