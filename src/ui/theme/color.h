#ifndef XER_UI_THEME_COLOR_H
#define XER_UI_THEME_COLOR_H

namespace XER::UI::Theme {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    bool operator==(const Color&) const = default;
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_COLOR_H
