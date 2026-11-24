// render/presenter.h — the consumer/sink end of the VideoEditor render
// port (slice 1): our analogue of Chromium's viz::Display +
// SharedImage consumer pair.
//
// The split mirrors the Chromium contract:
//   * PresentVideoFrame() — the producer hand-off ("mailbox"): the
//     compositor/render worker posts the newest finished frame;
//     coalescing (newest-wins) is the presenter's business, matching
//     D2DPresenter's single-slot job box.
//   * Composite() — the draw-time mapping onto the widget's render
//     canvas. Our widget tree is immediate-mode: Widget::Render() runs
//     inside Renderer::BeginFrame/EndFrame and paints into the keyed-
//     mutex shared canvas texture that
//     XER::UI::Rendering::Windows::D2DPresenter flip-presents. So
//     Composite(canvas, dst, opacity) is the "display compositor draw"
//     of viz — it lands inside the monitor surface's paint callback.
//
// Slice 1 deliberately keeps the interface storage-agnostic: CPU
// frames composite through the existing DrawFrameBgra upload; opaque
// texture frames return false until the SharedImage-style path lands
// (documented in d2d_presenter.cc — the plug point for R3/R4/R6).
//
// The forward-declared Renderer/Rect keep this header usable from
// UI-free TUs; only implementers and callers that actually Composite
// need the ui/ includes.
#pragma once

#include <cstdint>
#include <memory>

#include "video_frame.h"

namespace XER::UI::Layout {
struct Rect;
}
namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::VideoEditor::Render {

class VideoFramePresenter {
public:
    virtual ~VideoFramePresenter() = default;

    // Mailbox hand-off: stores `frame` as the next thing to display.
    // A not-yet-composited previous frame is discarded and counted as
    // coalesced — identical to D2DPresenter::Submit's newest-wins
    // contract and to Chromium's "latest mailbox wins" surface update.
    // Thread-safe; the producer (render worker) and consumer (UI paint
    // thread) are expected to be different threads.
    virtual void PresentVideoFrame(scoped_refptr<VideoFrame> frame) = 0;

    // Draws the mailboxed frame into `dst` (canvas coordinates, the
    // same space the widget's own draws use) on the render canvas.
    // Returns false when there is nothing to draw or the frame's
    // storage can't be composited yet (opaque texture — caller falls
    // back to its existing path). Consumes the mailboxed frame.
    virtual bool Composite(XER::UI::Rendering::Renderer& canvas,
                           XER::UI::Layout::Rect dst,
                           float opacity) = 0;

    // The last presented frame, retained for late-compositing pulls —
    // nullptr until the first PresentVideoFrame. Unlike Composite it
    // does not consume.
    virtual scoped_refptr<VideoFrame> CurrentFrame() const = 0;

    struct Counters {
        std::uint64_t presented = 0;         // mailbox submissions
        std::uint64_t coalesced = 0;         // overwritten pre-composite
        std::uint64_t composited = 0;        // successful draws
        std::uint64_t texture_frames = 0;    // kOpaqueTexture arrivals
        std::uint64_t failed_composites = 0; // Composite() -> false
    };
    virtual Counters counters() const = 0;
};

// Windows-only today: the D2D/D3D11-backed presenter skeleton
// (render/d2d_presenter.cc). Non-Windows builds get the declaration
// without a definition — callers must #if accordingly.
std::unique_ptr<VideoFramePresenter> CreateD2DVideoPresenter();

}  // namespace XER::VideoEditor::Render
