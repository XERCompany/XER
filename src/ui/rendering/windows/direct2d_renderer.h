#ifndef XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
#define XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

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

private:
    void EnsureRenderTarget();
    void RequireFrame() const;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(TextStyle style) const;
    D2D1_COLOR_F ToColor(Theme::Color color) const;

    HWND hwnd_;
    UINT dpi_;
    Theme::Color clear_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool frame_started_ = false;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
