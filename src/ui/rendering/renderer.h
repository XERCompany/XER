#ifndef XER_UI_RENDERING_RENDERER_H
#define XER_UI_RENDERING_RENDERER_H

#include <cstdint>
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
    // Fixed-pitch font request (code editors, terminals, numeric
    // columns). Renderers map this to the platform monospace face
    // (Consolas on Windows); false keeps the proportional UI font.
    bool monospace = false;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual void BeginFrame(Layout::Rect bounds) = 0;
    virtual void EndFrame() = 0;

    // Host-provided environment. Defaults are no-ops so streamed/remote
    // renderers compile without them.
    virtual void SetDpi(unsigned dpi) { (void)dpi; }
    virtual void SetClearColor(Theme::Color color) { (void)color; }

    // Optional damage rect (absolute client coords, same space as widget
    // bounds) for the upcoming frame. Retained-mode renderers use it to
    // skip clearing/redrawing unaffected areas; immediate renderers may
    // ignore it. Called by WidgetTree::Render before BeginFrame.
    virtual void SetFrameDamage(Layout::Rect damage) { (void)damage; }

    // True for exactly one frame after the renderer recreated its draw
    // targets (first frame, resize, DPI or device change). A fresh retained
    // canvas has no previous pixels, so WidgetTree must ignore damage
    // culling and repaint everything — otherwise undrawn regions keep the
    // clear color until they happen to be invalidated again.
    virtual bool TakeTargetsRecreated() { return false; }

    // ---- Diagnostics (XER_UI_PERF) ------------------------------------
    // Per-frame primitive counters, reset when read — the host calls this
    // right after Render() so the numbers describe the frame that just
    // ended. Renderers without counters return the zero struct.
    struct FrameCounters {
        std::uint32_t draw_calls = 0;   // fills/strokes/lines issued
        std::uint32_t text_calls = 0;   // DrawText calls
        std::uint32_t image_calls = 0;  // bitmap/video/resource draws
    };
    virtual FrameCounters TakeFrameCounters() { return {}; }

    // Snapshot pushed by the host once per frame (before BeginFrame) when
    // the perf HUD is enabled (XER_UI_PERF=1); nullptr disables it. The
    // renderer draws it on the swapchain back buffer during EndFrame so it
    // never lands in the retained canvas. Data always describes the
    // *previous* completed frame.
    struct PerfHudInfo {
        double frame_ms = 0.0;
        double avg_frame_ms = 0.0;
        float damage_pct = 100.0f;
        std::uint32_t draw_calls = 0;
        std::uint32_t text_calls = 0;
        std::uint32_t image_calls = 0;
        std::uint32_t widgets_painted = 0;
        std::uint32_t widgets_culled = 0;
    };
    virtual void SetPerfHud(const PerfHudInfo* info) { (void)info; }
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

    // ---- Element compositing & store-backed resources ------------
    // Offscreen layer for per-element compositing (CSS opacity,
    // blend modes, filters). Calls nest like a stack.
    virtual void PushLayer(float opacity, std::uint32_t blend_mode,
                           float blur) {
        (void)opacity;
        (void)blend_mode;
        (void)blur;
    }
    virtual void PopLayer() {}

    // 2x3 affine transform applied to subsequent draws; ResetTransform
    // restores identity.
    virtual void SetTransform(float a, float b, float c, float d,
                              float tx, float ty) {
        (void)a; (void)b; (void)c; (void)d; (void)tx; (void)ty;
    }
    virtual void ResetTransform() {}

    // Store-backed draws (RenderStore shared memory — remote renderers
    // resolve the id; local renderers receive the pixels directly via
    // the pixel-pointer overloads below).
    virtual bool DrawResource(std::uint32_t id, Layout::Rect dst,
                              float opacity, float corner_radius) {
        (void)id; (void)dst; (void)opacity; (void)corner_radius;
        return false;
    }
    virtual bool DrawVideoFrame(std::uint32_t id, Layout::Rect dst,
                                float opacity, float corner_radius) {
        (void)id; (void)dst; (void)opacity; (void)corner_radius;
        return false;
    }
    virtual bool DrawSpectrum(std::uint32_t id, Layout::Rect dst,
                              Theme::Color color, std::uint32_t mode) {
        (void)id; (void)dst; (void)color; (void)mode;
        return false;
    }
    virtual void FreeResource(std::uint32_t id) { (void)id; }

    // Pixel-resolved variants — the render worker's store resolver
    // calls these once it has the shared-memory pointer. Local
    // implementations can forward to DrawFrameBgra / per-bar rects.
    virtual bool DrawResourcePixels(const void* bgra, std::uint32_t id,
                                    int width, int height,
                                    Layout::Rect dst, float opacity,
                                    float corner_radius) {
        (void)id; (void)corner_radius;
        return DrawFrameBgra(bgra, width, height, dst, opacity);
    }
    virtual bool DrawVideoPixels(const void* bgra, std::uint32_t id,
                                 int width, int height, Layout::Rect dst,
                                 float opacity, float corner_radius) {
        (void)id; (void)corner_radius;
        return DrawFrameBgra(bgra, width, height, dst, opacity);
    }
    virtual bool DrawSpectrumPixels(const float* levels,
                                    std::uint32_t count,
                                    Layout::Rect dst, Theme::Color color,
                                    std::uint32_t mode) {
        if (levels == nullptr || count == 0) {
            return false;
        }
        const float bar_w = dst.width / count;
        const float half = static_cast<float>(dst.y + dst.height / 2.0f);
        for (std::uint32_t i = 0; i < count; ++i) {
            const float v = levels[i] < 0.f ? 0.f
                          : (levels[i] > 1.f ? 1.f : levels[i]);
            const float h = v * dst.height;
            Layout::Rect bar{
                static_cast<float>(dst.x + i * bar_w),
                mode == 2 ? half - h / 2.0f
                          : static_cast<float>(dst.y + dst.height - h),
                bar_w > 1.f ? bar_w - 1.f : bar_w,
                mode == 2 ? h : h};
            if (mode == 1) {  // line mode: thin column
                bar.height = bar.height > 2.f ? 2.f : bar.height;
            }
            FillRect(bar, color, 0.f);
        }
        return true;
    }
};

} // namespace XER::UI::Rendering

#endif // XER_UI_RENDERING_RENDERER_H
