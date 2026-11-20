#ifndef XER_UI_RENDERING_WINDOWS_SOFTWARE_RENDERER_H
#define XER_UI_RENDERING_WINDOWS_SOFTWARE_RENDERER_H

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Windows {

// CPU rasterizer: retained BGRA pixel buffer, presented to the HWND via
// GDI SetDIBitsToDevice. The always-available fallback backend — it needs
// no GPU, no DXGI, not even a window: a null HWND gives a headless
// renderer whose pixels tests can inspect through Pixels().
//
// Implemented: FillRect (incl. rounded corners + AA on axis-aligned
// quads), StrokeRect, DrawLine, DrawText/MeasureText (GDI coverage
// masks), DrawImage (WIC), DrawFrameBgra/DrawResourcePixels/
// DrawVideoPixels (bilinear-ish nearest sampling), PushClip/PopClip,
// PushLayer/PopLayer (opacity multiplier; blend modes/blur deferred —
// same approximation as Direct2DRenderer), SetTransform (arbitrary 2x3
// affine — geometry is scanline-filled as a convex quad), backdrop blur
// (real box blur of the retained buffer).
class SoftwareRenderer final : public Renderer {
public:
    // `hwnd` may be nullptr — headless mode keeps rasterizing into the
    // internal buffer but skips the GDI present in EndFrame.
    explicit SoftwareRenderer(HWND hwnd);
    ~SoftwareRenderer() override = default;

    SoftwareRenderer(const SoftwareRenderer&) = delete;
    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;

    void SetDpi(unsigned dpi) override;
    void SetClearColor(Theme::Color color) override;
    void SetFrameDamage(Layout::Rect damage) override;
    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    bool TakeTargetsRecreated() override;
    void FillRect(Layout::Rect rect, Theme::Color color,
                  float radius) override;
    void StrokeRect(Layout::Rect rect, Theme::Color color, float width,
                    float radius) override;
    void DrawLine(Layout::Point start, Layout::Point end,
                  Theme::Color color, float width) override;
    void DrawText(std::string_view text, Layout::Rect rect,
                  TextStyle style) override;
    Layout::Size MeasureText(std::string_view text,
                             TextStyle style) const override;
    void PushClip(Layout::Rect rect) override;
    void PopClip() override;
    void DrawBackdropBlur(Layout::Rect rect, float radius) override;
    bool DrawImage(std::string_view path, Layout::Rect rect,
                   float opacity, float corner_radius) override;
    bool DrawFrameBgra(const void* bgra, int width, int height,
                       Layout::Rect dst_rect, float opacity) override;
    void PushLayer(float opacity, std::uint32_t blend_mode,
                   float blur) override;
    void PopLayer() override;
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override;
    void ResetTransform() override;
    bool DrawResourcePixels(const void* bgra, std::uint32_t id,
                            int width, int height, Layout::Rect dst,
                            float opacity, float corner_radius) override;
    bool DrawVideoPixels(const void* bgra, std::uint32_t id,
                         int width, int height, Layout::Rect dst,
                         float opacity, float corner_radius) override;
    void FreeResource(std::uint32_t id) override;

    // Headless/test access to the retained frame buffer (BGRA, top-down,
    // PixelWidth() stride). nullptr until the first BeginFrame.
    const std::uint32_t* Pixels() const {
        return buffer_.empty() ? nullptr : buffer_.data();
    }
    int PixelWidth() const { return width_px_; }
    int PixelHeight() const { return height_px_; }

private:
    struct Mat2x3 {
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        float tx = 0.0f, ty = 0.0f;
    };
    struct Image {
        int w = 0;
        int h = 0;
        std::vector<std::uint8_t> bgra;
    };

    void RequireFrame() const;
    Layout::Rect DipRectToPixelQuad(Layout::Rect rect, float* corners) const;
    void BlendQuadPx(const float corners[8], Theme::Color color,
                     float radius_dip);
    void BlendImage(const Image& img, Layout::Rect dst_dip, float opacity,
                    float corner_radius);
    void BlendMask(const std::vector<std::uint8_t>& mask, int w, int h,
                   Layout::Point origin_px, Theme::Color color);
    void ClipTo(const float corners[8]);

    HWND hwnd_;
    unsigned dpi_;
    Theme::Color clear_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool frame_started_ = false;
    bool targets_recreated_ = false;
    bool buffer_valid_ = false;

    std::vector<std::uint32_t> buffer_;
    int width_px_ = 0;
    int height_px_ = 0;
    Layout::Rect bounds_dip_{};

    Mat2x3 transform_{};
    std::vector<Layout::Rect> clip_stack_px_;
    std::vector<float> layer_opacity_;

    std::unordered_map<std::string, Image> image_cache_;
    std::unordered_map<std::uint32_t, Image> resource_cache_;
    std::unordered_map<std::uint32_t, Image> video_cache_;
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_SOFTWARE_RENDERER_H
