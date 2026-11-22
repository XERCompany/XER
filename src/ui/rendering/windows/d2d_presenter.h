#ifndef XER_UI_RENDERING_WINDOWS_D2D_PRESENTER_H
#define XER_UI_RENDERING_WINDOWS_D2D_PRESENTER_H

// Dedicated presentation thread for the D2D engine.
//
// Motivation: on some driver/DWM combinations IDXGISwapChain::Present
// blocks ~one vsync even with an empty frame queue, a non-blocking
// present flag and a signaled latency object (measured 2026-11-22:
// 13.6 ms inside Present1 with S_OK). Any stall in Present therefore
// freezes WM_PAINT handling and input. The presenter owns the swapchain
// and a second D3D11 device on a background thread; the UI thread draws
// into a shared keyed-mutex texture and only hands off a job, so the
// worst present stall costs one dropped frame instead of a hung UI.
//
// GPU sync uses a keyed mutex on the shared canvas texture:
//   engine    (device A, UI thread):      AcquireSync(0) -> draw -> ReleaseSync(1)
//   presenter (device B, present thread): AcquireSync(1) -> copy -> ReleaseSync(0)
// This is the standard DXGI producer/consumer pattern — GPU-ordered,
// zero-copy, and the engine only ever waits ~the copy duration.

#include <atomic>
#include <cstdint>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

namespace XER::UI::Rendering::Windows {

class D2DPresenter {
public:
    // A finished frame offered for display. `shared_handle` is the
    // IDXGIResource::GetSharedHandle of the canvas texture (changes only
    // when the texture is recreated, tracked via `generation`).
    struct Frame {
        HANDLE shared_handle = nullptr;
        std::uint64_t generation = 0;
        std::uint32_t pixel_width = 0;
        std::uint32_t pixel_height = 0;
        // Dirty rects in pixels; count==0 means "present whole frame".
        RECT dirty[4]{};
        std::uint32_t dirty_count = 0;
        // Perf HUD overlay text drawn on the backbuffer (transient —
        // never part of the retained canvas). has_hud selects it.
        char hud_text[192]{};
        RECT hud_rect{};
        bool has_hud = false;
    };

    explicit D2DPresenter(HWND hwnd);
    ~D2DPresenter();

    D2DPresenter(const D2DPresenter&) = delete;
    D2DPresenter& operator=(const D2DPresenter&) = delete;

    // Queues a completed frame for display. Non-blocking: if a previous
    // frame is still being presented the slot is overwritten — only the
    // newest frame is ever presented (natural coalescing).
    void Submit(const Frame& frame);

    // Diagnostics: presents actually issued (vs submissions coalesced
    // away), and whether the thread is alive.
    std::uint64_t PresentedCount() const { return presented_.load(); }
    std::uint64_t DroppedCount() const { return dropped_.load(); }

    // Present-thread body — public so the plain WINAPI entry point can
    // call it; never invoke from the UI thread.
    void ThreadMain();

private:
    void EnsureSwapchain(std::uint32_t width, std::uint32_t height);
    bool EnsureDevice();
    bool OpenShared(const Frame& frame);
    void DropDevice();
    void DrawHud(const Frame& frame);

    HWND hwnd_ = nullptr;

    // Owned by the present thread only.
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_tex_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> shared_mutex_;
    // HUD overlay: tiny D2D surface over the backbuffer, created lazily
    // on the first HUD-bearing job (device B objects, present thread).
    Microsoft::WRL::ComPtr<ID2D1Factory1> hud_d2d_factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> hud_d2d_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> hud_dc_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> hud_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hud_brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> hud_write_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hud_format_;
    std::uint64_t opened_generation_ = 0;
    std::uint32_t swap_width_ = 0;
    std::uint32_t swap_height_ = 0;
    // Union of frames dropped while the queue was saturated — carried
    // into the next present's dirty list so no region stays stale.
    RECT stale_dirty_{};
    bool has_stale_ = false;
    // ResizeBuffers / swapchain creation leaves the backbuffer
    // undefined: only a full copy may follow, partial dirty rects would
    // leave permanent voids in the undefined region.
    bool backbuffer_dirty_ = true;

    // Single-slot job box: latest wins.
    SRWLOCK job_lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE job_cv_ = CONDITION_VARIABLE_INIT;
    Frame pending_{};
    bool has_pending_ = false;
    bool stop_ = false;

    HANDLE thread_ = nullptr;
    std::atomic<std::uint64_t> presented_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_D2D_PRESENTER_H
