// render/d2d_presenter.cc — D2D/D3D11 VideoFramePresenter skeleton.
//
// What this is (slice 1): the mailbox + Composite() half of the
// present path, riding the existing immediate-mode stack. Composite()
// today resolves CPU frames through Renderer::DrawFrameBgra — the
// Direct2DRenderer/Direct2DEngine implementations of that call do
// ID2D1Bitmap1::CopyFromMemory into a staging bitmap and draw it into
// the keyed-mutex shared canvas texture, which
// XER::UI::Rendering::Windows::D2DPresenter (src/ui/rendering/windows/
// d2d_presenter.cc — an existing, unrelated class with the same job:
// the swapchain-owning present thread) then copies to the backbuffer
// and Present1()s. That is already the Chromium single-process shape:
// producer (UI thread, device A) -> keyed-mutex shared texture ->
// consumer thread (device B) -> swapchain = our miniature viz::Display
// + GPU-process pair.
//
// Where the real GPU path plugs in (all marked PLUG POINT below):
//   1. VideoFrame::WrapTexture() delivers a kOpaqueTexture frame whose
//      VideoTexture::shared_handle is a D3D11 shared HANDLE (the
//      SharedImage/Mailbox pair collapsed to one process, plan §3).
//   2. Composite() resolves the handle on the canvas's D3D11 device:
//      OpenSharedResource -> IDXGIKeyedMutex::AcquireSync ->
//      CreateBitmapFromDxgiSurface (or CopySubresourceRegion into the
//      staging texture) -> DrawBitmap — replacing the CopyFromMemory
//      upload (copy D in plan §2) with a GPU-resident blit.
//      NOTE: the XER::UI::Rendering::Renderer interface has no
//      texture-draw entry point today — slice 2 adds either a
//      DrawVideoTexture(const VideoTexture&, dst) virtual or a
//      RenderStore texture-slot id, implemented by the D2D backends.
//   3. The ANGLE/Ganesh-GL variant wraps the same D3D11 texture via
//      eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE) for the
//      Skia-GPU canvas path (tickets R1/R2/R5).
//   4. Backpressure/BeginFrame: D2DPresenter's present thread already
//      owns the vsync boundary (flip-queued Present1); its post-
//      present ack is where the FrameScheduler tick hook lands (R6).
//
// What this is NOT: it does not touch MonitorSurface, PreviewBinding,
// or the compositor — wiring is the integration ticket.
#include "presenter.h"

#if defined(_WIN32)

#include <mutex>

#include "layout/geometry.h"
#include "rendering/renderer.h"

namespace XER::VideoEditor::Render {
namespace {

class D2DVideoPresenter final : public VideoFramePresenter {
public:
    void PresentVideoFrame(scoped_refptr<VideoFrame> frame) override {
        if (!frame) {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if (pending_) {
            ++counters_.coalesced;  // overwritten before compositing
        }
        pending_ = std::move(frame);
        ++counters_.presented;
    }

    bool Composite(XER::UI::Rendering::Renderer& canvas,
                   XER::UI::Layout::Rect dst, float opacity) override {
        scoped_refptr<VideoFrame> frame;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            frame = std::move(pending_);
            pending_.reset();
        }
        if (!frame) {
            ++counters_.failed_composites;
            return false;
        }
        // Retain a pull-path copy for CurrentFrame() consumers.
        {
            std::lock_guard<std::mutex> lk(mutex_);
            current_ = frame;
        }

        if (frame->storage_type() == VideoFrameStorage::kOpaqueTexture) {
            ++counters_.texture_frames;
            // PLUG POINT 1/2 — SharedImage/D3D11 path: resolve
            // frame->texture().shared_handle on the canvas's D3D11
            // device (keyed-mutex AcquireSync, then draw the texture
            // bitmap) instead of a CPU upload. Blocked on a texture
            // draw entry point in the Renderer interface — until then
            // report un-compositable so the caller keeps its CPU
            // fallback alive.
            ++counters_.failed_composites;
            return false;
        }
        if (!frame->IsMappable() || frame->planes() < 1 ||
            frame->data(0) == nullptr) {
            ++counters_.failed_composites;
            return false;
        }

        const VideoRect vis = frame->visible_rect();
        // bytes-per-pixel for the packed single-plane formats
        // (kBGRA8/kRGBA8 — 4). Multi-planar CPU frames (NV12/I420)
        // have no direct canvas draw; they exist for the texture
        // pipeline's software fallback and would need a YUV->BGRA
        // convert before this point.
        constexpr int kBpp = 4;
        if (frame->format() != VideoPixelFormat::kBGRA8 &&
            frame->format() != VideoPixelFormat::kRGBA8) {
            ++counters_.failed_composites;
            return false;
        }
        // DrawFrameBgra has no stride parameter — it reads packed
        // rows. When the visible rect doesn't span the coded width
        // (padded sources) or the stride isn't packed, this needs a
        // repack or a stride-aware upload; the texture path removes
        // the need entirely (PLUG POINT 2 above).
        if (frame->stride(0) != vis.width * kBpp) {
            ++counters_.failed_composites;
            return false;
        }
        const std::uint8_t* src = frame->data(0) +
                                  vis.y * frame->stride(0) +
                                  vis.x * kBpp;
        if (!canvas.DrawFrameBgra(src, vis.width, vis.height, dst,
                                  opacity)) {
            ++counters_.failed_composites;
            return false;
        }
        ++counters_.composited;
        return true;
    }

    scoped_refptr<VideoFrame> CurrentFrame() const override {
        std::lock_guard<std::mutex> lk(mutex_);
        return current_;
    }

    Counters counters() const override {
        std::lock_guard<std::mutex> lk(mutex_);
        return counters_;
    }

private:
    // Single-slot mailbox — newest wins (D2DPresenter::Submit parity).
    mutable std::mutex mutex_;
    scoped_refptr<VideoFrame> pending_;
    scoped_refptr<VideoFrame> current_;
    Counters counters_;
};

}  // namespace

std::unique_ptr<VideoFramePresenter> CreateD2DVideoPresenter() {
    return std::make_unique<D2DVideoPresenter>();
}

}  // namespace XER::VideoEditor::Render

#endif  // defined(_WIN32)
