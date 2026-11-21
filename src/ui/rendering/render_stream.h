#ifndef XER_UI_RENDERING_RENDER_STREAM_H
#define XER_UI_RENDERING_RENDER_STREAM_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "rendering/renderer.h"

namespace XER::UI::Rendering {

// Serialized render-command stream. Lets a UI process forward every
// Renderer call to a remote render process (xer_engine_ui_render.exe)
// over a byte transport — typically Cache::SharedMemoryChannel — so UI
// processing and GPU rendering live in separate processes.
//
// Wire format per message: [u32 op][op payload]. One transport message
// carries exactly one op. Layout::Point/Rect/Size, Theme::Color and
// TextStyle serialize as little-endian IEEE-754 floats (plus u32
// weight and a u32 flags word — bit0 = monospace) — fixed-layout
// PODs, no padding assumptions.
namespace Stream {

enum class Op : std::uint32_t {
    kBeginFrame = 1,
    kEndFrame,
    kSetFrameDamage,
    kFillRect,
    kStrokeRect,
    kDrawLine,
    kDrawText,
    kPushClip,
    kPopClip,
    kDrawBackdropBlur,
    kDrawImage,
    kDrawFrameBgra,
    kSetDpi,
    kSetClearColor,
    // Synchronous round-trip: host sends kMeasureRequest, the remote
    // process replies with kMeasureResponse carrying the same req id.
    kMeasureRequest,
    kMeasureResponse,
    // Retarget the worker's presentation surface to a (foreign) HWND —
    // pre-spawned pool workers adopt a window on checkout.
    // Payload: u64 HWND value; 0 = back to the worker's own surface.
    kSetTargetHwnd,
    // Render worker should tear down (window closed / stream ended).
    kShutdown,
    // ---- Resource-store ops (pixel payloads live in RenderStore
    // shared memory; the stream carries only ids) -----------------
    // Attach the worker to the named RenderStore. Payload: u16 name
    // length + UTF-8 name bytes.
    kAttachStore,
    // Draw a cached image resource (kImageBgra payload).
    // Payload: u32 id, Rect dst, f32 opacity, f32 corner_radius.
    kDrawResource,
    // Draw the latest frame of a kVideoFrame resource — the widget's
    // live video/capture surface.
    // Payload: u32 id, Rect dst, f32 opacity, f32 corner_radius.
    kDrawVideoFrame,
    // Free a resource slot in the store.
    // Payload: u32 id.
    kFreeResource,
    // Composite a subtree through an offscreen layer.
    // Payload: f32 opacity, u32 blend_mode, f32 blur.
    kPushLayer,
    kPopLayer,
    // Set/clear the current 2x3 affine transform (a,b,c,d,tx,ty).
    kSetTransform,
    kResetTransform,
    // Draw audio levels held by a kSpectrumF32 resource — VU meters,
    // spectrum bars, waveforms — without pushing PCM through the
    // channel every frame.
    // Payload: u32 id, Rect dst, Color color, u32 mode
    // (0=bars, 1=line, 2=mirror).
    kDrawSpectrum,
};

// Appends one serialized op message to `out`.
class StreamWriter {
public:
    static void BeginFrame(std::vector<uint8_t>& out,
                           Layout::Rect bounds);
    static void EndFrame(std::vector<uint8_t>& out);
    static void SetFrameDamage(std::vector<uint8_t>& out,
                               Layout::Rect damage);
    static void FillRect(std::vector<uint8_t>& out, Layout::Rect rect,
                         Theme::Color color, float radius);
    static void StrokeRect(std::vector<uint8_t>& out, Layout::Rect rect,
                           Theme::Color color, float width,
                           float radius);
    static void DrawLine(std::vector<uint8_t>& out, Layout::Point a,
                         Layout::Point b, Theme::Color color,
                         float width);
    static void DrawText(std::vector<uint8_t>& out, std::string_view text,
                         Layout::Rect rect, const TextStyle& style);
    static void PushClip(std::vector<uint8_t>& out, Layout::Rect rect);
    static void PopClip(std::vector<uint8_t>& out);
    static void DrawBackdropBlur(std::vector<uint8_t>& out,
                                 Layout::Rect rect, float radius);
    static void DrawImage(std::vector<uint8_t>& out,
                          std::string_view path, Layout::Rect rect,
                          float opacity, float corner_radius);
    static void DrawFrameBgra(std::vector<uint8_t>& out,
                              const void* bgra, int width, int height,
                              Layout::Rect dst_rect, float opacity);
    static void SetDpi(std::vector<uint8_t>& out, unsigned dpi);
    static void SetClearColor(std::vector<uint8_t>& out,
                              Theme::Color color);
    static void MeasureRequest(std::vector<uint8_t>& out,
                               std::uint32_t request_id,
                               std::string_view text,
                               const TextStyle& style);
    static void MeasureResponse(std::vector<uint8_t>& out,
                                std::uint32_t request_id,
                                Layout::Size size);
    static void SetTargetHwnd(std::vector<uint8_t>& out,
                              std::uint64_t hwnd);
    static void Shutdown(std::vector<uint8_t>& out);
    static void AttachStore(std::vector<uint8_t>& out,
                            std::string_view name);
    static void DrawResource(std::vector<uint8_t>& out, std::uint32_t id,
                             Layout::Rect dst, float opacity,
                             float corner_radius);
    static void DrawVideoFrame(std::vector<uint8_t>& out,
                               std::uint32_t id, Layout::Rect dst,
                               float opacity, float corner_radius);
    static void FreeResource(std::vector<uint8_t>& out,
                             std::uint32_t id);
    static void PushLayer(std::vector<uint8_t>& out, float opacity,
                          std::uint32_t blend_mode, float blur);
    static void PopLayer(std::vector<uint8_t>& out);
    static void SetTransform(std::vector<uint8_t>& out, float a, float b,
                             float c, float d, float tx, float ty);
    static void ResetTransform(std::vector<uint8_t>& out);
    static void DrawSpectrum(std::vector<uint8_t>& out, std::uint32_t id,
                             Layout::Rect dst, Theme::Color color,
                             std::uint32_t mode);
};

// Decodes incoming stream messages. `on_measure` answers MeasureText
// requests (the worker calls its real renderer). `on_shutdown` fires
// once on kShutdown.
class StreamReader {
public:
    struct Handlers {
        // Return true when the op was consumed here and must NOT reach
        // the renderer (measure requests / shutdown).
        std::function<Layout::Size(std::uint32_t req_id,
                                   std::string_view text,
                                   const TextStyle& style)>
            on_measure;
        std::function<void()> on_shutdown;
        // kSetTargetHwnd — the worker recreates its render surface on
        // this HWND (or its own hidden surface when 0).
        std::function<void(std::uint64_t hwnd)> on_retarget;
        // kAttachStore — the worker opens the named RenderStore and
        // resolves kDrawResource / kDrawVideoFrame / kDrawSpectrum ids.
        std::function<void(std::string_view name)> on_attach_store;
        // Resource lookup used by Dispatch for store-backed draws.
        // Return the payload pointer + fill meta; nullptr when the id
        // is absent or a write is in flight.
        struct ResourceMeta {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t payload = 0;
            std::uint32_t kind = 0;
        };
        std::function<const void*(std::uint32_t id, ResourceMeta* meta)>
            on_resource;
    };

    // Dispatches `message` (one serialized op) to `renderer` or the
    // handlers. Returns false for malformed/truncated input and for ops
    // the worker loop must handle itself (kMeasureRequest /
    // kMeasureResponse).
    static bool Dispatch(Renderer& renderer, const Handlers& handlers,
                         const uint8_t* message, uint32_t bytes);

    // Reads the op id without consuming the payload.
    static bool PeekOp(const uint8_t* message, uint32_t bytes, Op* op);
    // Decodes a kMeasureRequest payload.
    static bool ParseMeasureRequest(const uint8_t* message,
                                    uint32_t bytes,
                                    std::uint32_t* request_id,
                                    std::string* text, TextStyle* style);
    // Decodes a kMeasureResponse payload.
    static bool ParseMeasureResponse(const uint8_t* message,
                                     uint32_t bytes,
                                     std::uint32_t* request_id,
                                     Layout::Size* size);
};

}  // namespace Stream

// Renderer that serializes every call into stream messages and pushes
// them to `send` (e.g. SharedMemoryChannel::Send). MeasureText issues a
// synchronous request/response through `request` which must block until
// the remote reply arrives (the render worker replies immediately).
//
//   send   : void(std::vector<uint8_t>&&) fire-and-forget op
//   request: Layout::Size(std::vector<uint8_t>&&) round-trip op
class StreamRenderer final : public Renderer {
public:
    using SendFn = std::function<bool(std::vector<uint8_t>&&)>;
    using RequestFn =
        std::function<Layout::Size(std::vector<uint8_t>&&, std::uint32_t)>;
    // Called once from the destructor — the render host uses it to
    // release the worker back to the warm pool (or reap its entry).
    using ReleaseFn = std::function<void()>;

    StreamRenderer(SendFn send, RequestFn request,
                   ReleaseFn on_destroy = {});
    ~StreamRenderer() override;

    void BeginFrame(Layout::Rect bounds) override;
    void EndFrame() override;
    void SetFrameDamage(Layout::Rect damage) override;
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
    void SetDpi(unsigned dpi) override;
    void SetClearColor(Theme::Color color) override;
    void PushLayer(float opacity, std::uint32_t blend_mode,
                   float blur) override;
    void PopLayer() override;
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override;
    void ResetTransform() override;
    bool DrawResource(std::uint32_t id, Layout::Rect dst, float opacity,
                      float corner_radius) override;
    bool DrawVideoFrame(std::uint32_t id, Layout::Rect dst,
                        float opacity, float corner_radius) override;
    bool DrawSpectrum(std::uint32_t id, Layout::Rect dst,
                      Theme::Color color,
                      std::uint32_t mode) override;
    void FreeResource(std::uint32_t id) override;
    // Binds the renderer to a named RenderStore — emits kAttachStore
    // so the worker opens the same shared segment.
    void AttachStore(std::string_view name);

private:
    bool Emit(std::vector<uint8_t>&& msg);
    Layout::Size Request(std::vector<uint8_t>&& msg, std::uint32_t id) const;

    SendFn send_;
    RequestFn request_;
    ReleaseFn on_destroy_;
    mutable std::uint32_t next_request_id_ = 1;
};

}  // namespace XER::UI::Rendering

#endif  // XER_UI_RENDERING_RENDER_STREAM_H
