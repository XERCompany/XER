#ifndef XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
#define XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Windows {

// GPU renderer: D3D11 device -> D2D1 device context -> DXGI flip-model
// swapchain. The widget tree is rendered into a retained canvas bitmap and
// blitted to the swapchain back buffer each frame, so partial (damage-rect)
// repaints only re-fill the affected pixels instead of the whole window.
class Direct2DRenderer final : public Renderer {
public:
    explicit Direct2DRenderer(HWND hwnd);
    ~Direct2DRenderer() override;

    Direct2DRenderer(const Direct2DRenderer&) = delete;
    Direct2DRenderer& operator=(const Direct2DRenderer&) = delete;

    void SetClearColor(Theme::Color color) override;
    void SetDpi(unsigned dpi) override;
    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    bool TakeTargetsRecreated() override { return targets_recreated_; }
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
    void EnsureDevice();
    void EnsureTargets(UINT32 width, UINT32 height);
    void ReleaseTargets();
    void RequireFrame() const;
    ID2D1SolidColorBrush* Brush();
    Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(TextStyle style) const;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadImageBitmap(const std::string& path);
    D2D1_COLOR_F ToColor(Theme::Color color) const;

    HWND hwnd_;
    UINT dpi_;
    Theme::Color clear_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool frame_started_ = false;
    // True once the retained canvas holds a complete presented frame;
    // a fresh canvas must be fully cleared before reuse.
    bool canvas_valid_ = false;
    // Set by EnsureTargets when it (re)creates the canvas/backbuffer pair;
    // cleared at the start of each BeginFrame. Read via TakeTargetsRecreated.
    bool targets_recreated_ = false;
    D2D1_SIZE_U target_size_{0, 0};

    // D3D11 device shared by the DXGI swapchain and the D2D device.
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    // D2D device + immediate context issuing all draw commands.
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    // Retained scene: the full last-presented frame. Widget drawing targets
    // this bitmap; the swapchain back buffer only receives a blit of it.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> canvas_;
    // Render target view of the current swapchain back buffer.
    Microsoft::WRL::ComPtr<IDXGISurface> backbuffer_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> backbuffer_target_;
    // Reusable drawing resources. The brush is bound to the D2D device and
    // works on any of its targets.
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
