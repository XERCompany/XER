// YA.FrameRender — single seam for "how do I get this frame on screen".
//
// Monitor/preview/poster/export paths each used to re-implement the
// same two decisions: (1) normalize the producer's frame to packed
// BGRA8, and (2) choose the transport — RenderStore shared-memory
// upload + DrawVideoFrame(id) when the render worker pool is live
// (the worker replays it through whatever backend it picked, Vulkan
// included), or the in-process pixel path (DrawVideoPixels /
// DrawFrameBgra) which D2D/Vulkan backends stage to the GPU
// themselves. This module owns both decisions so callers stop
// duplicating them.
//
// The GPU path is a runtime choice, never a link dependency: the
// module loads no Vulkan library itself (probe only) and degrades to
// the CPU pixel path when no store is attached or the renderer
// declines the store id draw.
//
// Typical consumer (UI side):
//   XER::FrameRender::Service fr;
//   fr.Init({.store = UiRenderHost::Instance().Store()});  // or nullptr
//   fr.Present(renderer, frame, dest_rect, 1.0f);
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rendering/renderer.h"   // XER::UI::Rendering::Renderer
#include "video/frame.h"

namespace XER::UI::Rendering {
class RenderStore;   // ui/rendering/render_store.h — borrowed pointer
}

namespace XER::FrameRender {

// The transport a Present() call actually used.
enum class Backend {
    kNone = 0,      // nothing was drawn
    kCpu,           // in-process pixel path (DrawVideoPixels/DrawFrameBgra)
    kRenderStore,   // shared-memory slot + DrawVideoFrame(id)
};
const char* BackendName(Backend backend);

// Module capability matrix — informational, always constructible.
struct Probe {
    bool cpu_normalize = true;         // BGRA/RGBA/NV12/YUV420P -> BGRA8
    bool render_store_compiled = false;  // built with RenderStore support
    bool vulkan_loader = false;        // vulkan-1.dll loadable (info only —
                                       // in-process Vulkan renderers load it
                                       // themselves)
    std::string Report() const;
};
Probe ProbeFrameRender();

struct Options {
    // Non-owned shared store — typically UiRenderHost::Instance().Store()
    // when render workers are live, nullptr otherwise. The service
    // writes the kVideoFrame slot only when this is set AND
    // allow_store_upload is true.
    XER::UI::Rendering::RenderStore* store = nullptr;
    // RenderStore slot id used for the live frame — pick per-widget
    // values that do not collide with other users (host image uploads
    // use caller-chosen ids; 240 is reserved for monitor/preview).
    std::uint32_t video_slot = 240;
    // Master switch for the store path — when false the service always
    // draws through the pixel path even with a store attached.
    bool allow_store_upload = true;
};

// Frame-render service. Cheap to construct; Init() only records
// options — every per-frame decision happens inside Present().
// NOT internally synchronized — drive it from the paint thread.
class Service {
public:
    Service() = default;
    ~Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    bool Init(const Options& options = {},
              std::string* error_out = nullptr);
    void Shutdown();
    bool IsReady() const { return ready_; }

    // Backend the LAST Present() used (kNone before the first call or
    // after a failure). The configured backend is a preference — the
    // actual transport is per-frame because store uploads can fail.
    Backend LastBackend() const;
    const char* LastPathTaken() const;   // "store" | "pixels" | "none"
    std::uint32_t VideoSlot() const { return options_.video_slot; }

    // Normalize `src` to packed BGRA8 (stride == width*4) in `bgra`.
    // Handles every video::PixelFormat: BGRA8 copy, RGBA8 swizzle,
    // NV12 and YUV420P via the BT.601 converter. Static — usable
    // without a Service for pure CPU consumers (posters, exports).
    static bool Normalize(const video::Frame& src,
                          std::vector<std::uint8_t>* bgra,
                          int* width, int* height);

    // Presents `frame` at `dst` (renderer-space rect). Frame formats
    // other than BGRA8 are normalized first; BGRA8 is presented
    // zero-copy. Returns false only when nothing was drawn.
    bool Present(XER::UI::Rendering::Renderer& renderer,
                 const video::Frame& frame,
                 XER::UI::Layout::Rect dst, float opacity = 1.0f);

    // Same transport choice for callers that already hold packed
    // BGRA8 bytes (e.g. the monitor widget's own buffer) — no
    // normalization, no copy.
    bool PresentBgra(XER::UI::Rendering::Renderer& renderer,
                     const void* bgra, int width, int height,
                     XER::UI::Layout::Rect dst, float opacity = 1.0f);

private:
    Options options_{};
    bool ready_ = false;
    Backend last_backend_ = Backend::kNone;
    std::vector<std::uint8_t> scratch_;   // normalize workspace
};

}  // namespace XER::FrameRender
