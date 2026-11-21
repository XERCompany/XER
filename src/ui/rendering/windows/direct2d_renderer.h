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

#include <list>
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
    // Draws the perf overlay onto the current target (the swapchain back
    // buffer); returns the box it painted in DIP coords (for the dirty
    // rect list) or nullptr when nothing was drawn.
    bool DrawPerfHud(Layout::Rect* box_out);

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
    // Text caches are true LRU maps (order list front = most recent):
    // the previous "clear everything at N entries" eviction dropped the
    // whole working set whenever a changing string (timecode, meters)
    // filled the map, causing a visible hitch every few seconds.
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
    // Scratch key buffer reused by DrawText/MeasureText — avoids the
    // per-call std::to_string temporaries on the hot path.
    mutable std::string key_scratch_;

    // Damage rect for the frame in progress (DIP). SetFrameDamage records
    // it before BeginFrame; BeginFrame snapshots it into
    // `active_damage_` so frames started without a damage hint default to
    // a full present.
    Layout::Rect pending_damage_{};
    bool pending_damage_valid_ = false;
    Layout::Rect active_damage_{};
    bool active_damage_valid_ = false;

    // XER_UI_PERF diagnostics: counters incremented by each draw call,
    // consumed via TakeFrameCounters(); `perf_hud_` holds the stats the
    // host wants overlaid on the presented frame.
    FrameCounters counters_;
    PerfHudInfo hud_{};
    bool hud_enabled_ = false;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hud_format_;
    // XER_UI_PRESENT_FULL=1 forces the legacy whole-canvas blit + plain
    // Present for A/B comparison of the dirty-rect path.
    bool force_full_present_ = false;
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
    // Store-backed resources (RenderStore ids). kImageBgra entries
    // are cached once; kVideoFrame entries keep a per-id staging
    // bitmap that is rewritten every frame.
    std::unordered_map<std::uint32_t,
                       Microsoft::WRL::ComPtr<ID2D1Bitmap>>
        resource_cache_;
    struct VideoStage {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        D2D1_SIZE_U size{0, 0};
    };
    std::unordered_map<std::uint32_t, VideoStage> video_stages_;
    std::uint32_t layer_depth_ = 0;
    void DrawBitmapRounded(ID2D1Bitmap* bitmap, Layout::Rect dst,
                           float opacity, float corner_radius);
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_DIRECT2D_RENDERER_H
