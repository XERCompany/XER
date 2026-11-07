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
    // Font weight in the DirectWrite range (100..900). 400 = regular,
    // 500 = medium, 600+ = semibold/bold.
    int weight = 400;
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

    // Scissor clip in absolute client coordinates. PushClip/PopClip must
    // nest like a stack; drawing outside the top clip is discarded.
    // Default is a no-op so renderers without clip support still compile.
    virtual void PushClip(Layout::Rect rect) { (void)rect; }
    virtual void PopClip() {}

    // Frosted-glass backdrop (Tailwind `backdrop-blur`): captures the pixels
    // currently behind `rect` and paints a blurred copy. `radius` is the
    // approximate blur strength in the renderer's coordinate units.
    // Default no-op for renderers without screen access.
    virtual void DrawBackdropBlur(Layout::Rect rect, float radius) {
        (void)rect;
        (void)radius;
    }

    // Draws an image file (decoded through WIC on Windows) stretched into
    // `rect`, clipped to `corner_radius` (a value >= half the smaller side
    // produces a circle/ellipse). Decoded bitmaps are cached by path.
    // Returns false when the path is empty or the image cannot be decoded,
    // so callers can fall back to a placeholder.
    virtual bool DrawImage(std::string_view path, Layout::Rect rect,
                           float opacity = 1.0f, float corner_radius = 0.0f) {
        (void)path;
        (void)rect;
        (void)opacity;
        (void)corner_radius;
        return false;
    }

    // Uploads a packed BGRA frame (4 bytes/px in B,G,R,A order — the same
    // layout FFmpeg/libyuv produce) and draws it stretched into `dst_rect`.
    // Live-video path: implementations may keep a GPU staging bitmap and
    // should be prepared for per-frame calls. Returns false when the
    // renderer lacks frame support or the arguments are invalid.
    virtual bool DrawFrameBgra(const void* bgra, int width, int height,
                               Layout::Rect dst_rect,
                               float opacity = 1.0f) {
        (void)bgra;
        (void)width;
        (void)height;
        (void)dst_rect;
        (void)opacity;
        return false;
    }
};

} // namespace XER::UI::Rendering

#endif // XER_UI_RENDERING_RENDERER_H
