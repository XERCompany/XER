#ifndef XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
#define XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Windows {

class Direct2DRenderer final : public Renderer {
public:
    explicit Direct2DRenderer(HWND hwnd);
    ~Direct2DRenderer() override;

    Direct2DRenderer(const Direct2DRenderer&) = delete;
    Direct2DRenderer& operator=(const Direct2DRenderer&) = delete;

    void SetClearColor(Theme::Color color);
    void SetDpi(UINT dpi);
    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    void FillRect(Layout::Rect rect, Theme::Color color, float radius) override;
    void StrokeRect(Layout::Rect rect, Theme::Color color, float width, float radius) override;
    void DrawLine(
        Layout::Point start,
        Layout::Point end,
        Theme::Color color,
        float width
    ) override;
    void DrawText(std::string_view text, Layout::Rect rect, TextStyle style) override;
    Layout::Size MeasureText(std::string_view text, TextStyle style) const override;
    void PushClip(Layout::Rect rect) override;
    void PopClip() override;
    void DrawBackdropBlur(Layout::Rect rect, float radius) override;
    bool DrawImage(std::string_view path, Layout::Rect rect,
                   float opacity, float corner_radius) override;
    bool DrawFrameBgra(const void* bgra, int width, int height,
                       Layout::Rect dst_rect, float opacity) override;

private:
    void EnsureRenderTarget();
    void RequireFrame() const;
    ID2D1SolidColorBrush* Brush();
    Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(TextStyle style) const;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadImageBitmap(const std::string& path);
    D2D1_COLOR_F ToColor(Theme::Color color) const;

    HWND hwnd_;
    UINT dpi_;
    Theme::Color clear_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool frame_started_ = false;
    D2D1_SIZE_U target_size_{0, 0};
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    // Reusable drawing resources. The brush is bound to the current render
    // target and is recreated whenever the target is.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    mutable std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteTextFormat>> text_formats_;
    mutable std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteTextLayout>> text_layouts_;
    mutable std::unordered_map<std::string, Layout::Size> measure_cache_;
    // Cached bitmap reused by DrawBackdropBlur (recreated on size change).
    Microsoft::WRL::ComPtr<ID2D1Bitmap> blur_bitmap_;
    D2D1_SIZE_U blur_bitmap_size_{0, 0};
    // Decoded image cache for DrawImage, keyed by UTF-8 path. A present key
    // with a null bitmap means the file failed to decode (don't retry).
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>> image_cache_;
    // GPU staging bitmap for DrawFrameBgra — recreated only when the
    // incoming frame dimensions change; pixels are copied in per frame.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frame_bitmap_;
    D2D1_SIZE_U frame_bitmap_size_{0, 0};
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
