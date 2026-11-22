#ifndef XER_UI_RENDERING_WINDOWS_DIRECT2D_ENGINE_H
#define XER_UI_RENDERING_WINDOWS_DIRECT2D_ENGINE_H

// Next-generation Direct2D renderer ("d2d2").
//
// Same Renderer interface and feature set as Direct2DRenderer, with a
// different presentation architecture:
//
//   UI thread (device A)          present thread (device B)
//   --------------------          -------------------------
//   widgets -> shared canvas  ->  copy -> backbuffer -> Present
//   (keyed-mutex texture)         (may block — costs a frame, not the UI)
//
// Present() blocking observed on some driver/DWM pairs (~13 ms inside
// Present1 with a free queue, 2026-11-22) can no longer stall WM_PAINT
// or input. Frame hand-off is coalesced (newest wins) inside
// D2DPresenter; GPU ordering uses a DXGI keyed mutex on the canvas.

#include <windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Windows {

class D2DPresenter;

class Direct2DEngine final : public Renderer {
public:
    explicit Direct2DEngine(HWND hwnd);
    ~Direct2DEngine() override;

    Direct2DEngine(const Direct2DEngine&) = delete;
    Direct2DEngine& operator=(const Direct2DEngine&) = delete;

    void SetClearColor(Theme::Color color) override;
    void SetDpi(unsigned dpi) override;
    void SetFrameDamage(Layout::Rect damage) override;
    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    bool TakeTargetsRecreated() override { return targets_recreated_; }
    FrameCounters TakeFrameCounters() override;
    void SetPerfHud(const PerfHudInfo* info) override;
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
    void PushLayer(float opacity, std::uint32_t blend_mode,
                   float blur) override;
    void PopLayer() override;
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override;
    void ResetTransform() override;
    bool DrawResourcePixels(const void* bgra, std::uint32_t id,
                            int width, int height, Layout::Rect dst,
                            float opacity,
                            float corner_radius) override;
    bool DrawVideoPixels(const void* bgra, std::uint32_t id,
                         int width, int height, Layout::Rect dst,
                         float opacity,
                         float corner_radius) override;
    void FreeResource(std::uint32_t id) override;

private:
    void EnsureDevice();
    void EnsureTargets(UINT32 width, UINT32 height);
    void ReleaseTargets();
    void RequireFrame() const;
    ID2D1SolidColorBrush* Brush();
    Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(TextStyle style) const;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadImageBitmap(const std::string& path);
    D2D1_COLOR_F ToColor(Theme::Color color) const;
    bool FormatPerfHud(char* out, std::size_t cap) const;
    void DrawBitmapRounded(ID2D1Bitmap* bitmap, Layout::Rect dst,
                           float opacity, float corner_radius);
    void DropDeviceState();

    HWND hwnd_;
    UINT dpi_;
    Theme::Color clear_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool frame_started_ = false;
    bool canvas_valid_ = false;
    bool targets_recreated_ = false;
    D2D1_SIZE_U target_size_{0, 0};

    // D3D11 device feeding the D2D context (no swapchain here — the
    // presenter owns it on another thread/device).
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;

    // Retained scene AND the hand-off surface: a keyed-mutex shared
    // texture the presenter copies to the swapchain. `canvas_bitmap_`
    // is the D2D view used as the draw target.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> canvas_tex_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> canvas_mutex_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> canvas_;
    HANDLE canvas_shared_handle_ = nullptr;
    std::uint64_t canvas_generation_ = 0;
    bool canvas_mutex_held_ = false;

    std::unique_ptr<D2DPresenter> presenter_;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    mutable std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDWriteTextFormat>> text_formats_;
    template <typename T>
    struct LruEntry {
        using value_type = T;
        T value;
        std::list<std::string>::iterator lru_it;
    };
    mutable std::list<std::string> text_layout_lru_;
    mutable std::unordered_map<
        std::string, LruEntry<Microsoft::WRL::ComPtr<IDWriteTextLayout>>>
        text_layouts_;
    mutable std::list<std::string> measure_lru_;
    mutable std::unordered_map<std::string, LruEntry<Layout::Size>>
        measure_cache_;
    mutable std::string key_scratch_;

    Layout::Rect pending_damage_{};
    bool pending_damage_valid_ = false;
    Layout::Rect active_damage_{};
    bool active_damage_valid_ = false;

    FrameCounters counters_;
    PerfHudInfo hud_{};
    bool hud_enabled_ = false;
    bool force_full_present_ = false;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> blur_bitmap_;
    D2D1_SIZE_U blur_bitmap_size_{0, 0};
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>> image_cache_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> frame_bitmap_;
    D2D1_SIZE_U frame_bitmap_size_{0, 0};
    std::unordered_map<std::uint32_t,
                       Microsoft::WRL::ComPtr<ID2D1Bitmap>>
        resource_cache_;
    struct VideoStage {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        D2D1_SIZE_U size{0, 0};
    };
    std::unordered_map<std::uint32_t, VideoStage> video_stages_;
    std::uint32_t layer_depth_ = 0;
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_DIRECT2D_ENGINE_H
