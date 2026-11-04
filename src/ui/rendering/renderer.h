#ifndef XER_UI_RENDERING_RENDERER_H
#define XER_UI_RENDERING_RENDERER_H

#include <string_view>

#include "layout/geometry.h"
#include "theme/color.h"

// windows.h (included transitively by platform headers) defines DrawText as
// DrawTextA/DrawTextW. Undefine it so the Renderer interface keeps its declared
// name regardless of include order in translation units mixing WinAPI and UI.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::UI::Rendering {

struct TextStyle {
    Theme::Color color;
    float size = 14.0f;
    bool bold = false;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual void BeginFrame(Layout::Rect bounds) = 0;
    virtual void EndFrame() = 0;
    virtual void FillRect(Layout::Rect rect, Theme::Color color, float radius) = 0;
    virtual void StrokeRect(Layout::Rect rect, Theme::Color color, float width, float radius) = 0;
    virtual void DrawLine(
        Layout::Point start,
        Layout::Point end,
        Theme::Color color,
        float width
    ) = 0;
    virtual void DrawText(std::string_view text, Layout::Rect rect, TextStyle style) = 0;
    virtual Layout::Size MeasureText(std::string_view text, TextStyle style) const = 0;
};

} // namespace XER::UI::Rendering

#endif // XER_UI_RENDERING_RENDERER_H
