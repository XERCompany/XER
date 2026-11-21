// frame_render.cc — see frame_render.h for the contract. The CPU
// compositor seam (Normalize) is always available; the RenderStore
// transport is compiled in only when the module was built against the
// UI render-IPC target (XER_FR_HAS_RENDER_STORE), and engaged at run
// time only when a live store was attached via Options.
#include "frame_render/frame_render.h"

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#if XER_FR_HAS_RENDER_STORE
    #include "rendering/render_store.h"
#endif

#include <algorithm>

namespace XER::FrameRender {
namespace {

bool VulkanLoaderPresent() {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA("vulkan-1.dll");
    if (h == nullptr) return false;
    FreeLibrary(h);
    return true;
#else
    void* h = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (h == nullptr) {
        h = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (h == nullptr) return false;
    dlclose(h);
    return true;
#endif
}

// BT.601 limited-range YUV -> BGRA, scalar. The GPU path never touches
// this — it only runs for NV12/YUV420P sources on the CPU seam, where
// correctness beats vectorization (the sibling decoders emit BGRA
// already, so this is the minority path).
inline std::uint8_t Clamp8(int v) {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

void YuvToBgraScalar(const std::uint8_t* y_plane, std::size_t y_stride,
                     const std::uint8_t* u_plane,
                     const std::uint8_t* v_plane,
                     std::size_t uv_stride, bool uv_interleaved,
                     std::uint8_t* dst, int width, int height) {
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* ys = y_plane + row * y_stride;
        const std::uint8_t* us =
            u_plane + (row / 2) * uv_stride;
        const std::uint8_t* vs = uv_interleaved
            ? us + 1
            : v_plane + (row / 2) * uv_stride;
        const std::size_t u_step = uv_interleaved ? 2 : 1;
        std::uint8_t* out = dst + static_cast<std::size_t>(row) * width * 4;
        for (int col = 0; col < width; ++col) {
            const int y = ys[col];
            const int u = us[(col / 2) * u_step];
            const int v = vs[(col / 2) * u_step];
            const int c = y - 16;
            const int d = u - 128;
            const int e = v - 128;
            out[col * 4 + 0] = Clamp8((298 * c + 516 * d + 128) >> 8);
            out[col * 4 + 1] =
                Clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
            out[col * 4 + 2] = Clamp8((298 * c + 409 * e + 128) >> 8);
            out[col * 4 + 3] = 255;
        }
    }
}

}  // namespace

const char* BackendName(Backend backend) {
    switch (backend) {
        case Backend::kCpu: return "cpu";
        case Backend::kRenderStore: return "render_store";
        case Backend::kNone: return "none";
    }
    return "unknown";
}

Probe ProbeFrameRender() {
    Probe p;
#if XER_FR_HAS_RENDER_STORE
    p.render_store_compiled = true;
#endif
    p.vulkan_loader = VulkanLoaderPresent();
    return p;
}

std::string Probe::Report() const {
    std::string s;
    const auto line = [&s](const char* k, bool v) {
        s += "  ";
        s += k;
        s += v ? ": yes\n" : ": no\n";
    };
    line("cpu normalize path", cpu_normalize);
    line("render_store transport compiled", render_store_compiled);
    line("vulkan loader present", vulkan_loader);
    return s;
}

bool Service::Init(const Options& options, std::string* error_out) {
    options_ = options;
#if !XER_FR_HAS_RENDER_STORE
    if (options_.store != nullptr) {
        // A store was handed in but this build cannot write it —
        // drop the pointer so Present() takes the pixel path instead
        // of pretending the upload happened.
        options_.store = nullptr;
        options_.allow_store_upload = false;
        if (error_out != nullptr) {
            *error_out =
                "RenderStore transport not compiled into YA.FrameRender";
        }
    }
#endif
    ready_ = true;
    last_backend_ = Backend::kNone;
    return true;
}

void Service::Shutdown() {
    options_ = Options{};
    ready_ = false;
    last_backend_ = Backend::kNone;
    scratch_.clear();
}

Backend Service::LastBackend() const {
    return last_backend_;
}

const char* Service::LastPathTaken() const {
    switch (last_backend_) {
        case Backend::kRenderStore: return "store";
        case Backend::kCpu: return "pixels";
        case Backend::kNone: return "none";
    }
    return "none";
}

bool Service::Normalize(const video::Frame& src,
                        std::vector<std::uint8_t>* bgra, int* width,
                        int* height) {
    if (bgra == nullptr || width == nullptr || height == nullptr) {
        return false;
    }
    const int w = src.Width();
    const int h = src.Height();
    if (!src.IsValid() || w <= 0 || h <= 0) {
        return false;
    }
    const std::size_t need =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    switch (src.Format()) {
        case video::PixelFormat::BGRA8:
            if (src.DataSize() < need) return false;
            bgra->assign(src.Data(), src.Data() + need);
            break;
        case video::PixelFormat::RGBA8: {
            if (src.DataSize() < need) return false;
            bgra->resize(need);
            const std::uint8_t* s = src.Data();
            std::uint8_t* d = bgra->data();
            for (std::size_t i = 0; i + 3 < need; i += 4) {
                d[i + 0] = s[i + 2];   // B <- R
                d[i + 1] = s[i + 1];
                d[i + 2] = s[i + 0];   // R <- B
                d[i + 3] = s[i + 3];
            }
            break;
        }
        case video::PixelFormat::NV12: {
            const std::size_t y_bytes =
                static_cast<std::size_t>(w) * h;
            if (src.DataSize() < y_bytes * 3 / 2) return false;
            bgra->resize(need);
            const std::uint8_t* y_plane = src.Data();
            const std::uint8_t* uv_plane = y_plane + y_bytes;
            YuvToBgraScalar(y_plane, static_cast<std::size_t>(w),
                            uv_plane, nullptr,
                            static_cast<std::size_t>(w),
                            /*uv_interleaved=*/true, bgra->data(), w, h);
            break;
        }
        case video::PixelFormat::YUV420P: {
            const std::size_t y_bytes =
                static_cast<std::size_t>(w) * h;
            const std::size_t uv_plane_bytes = y_bytes / 4;
            if (src.DataSize() < y_bytes + 2 * uv_plane_bytes) {
                return false;
            }
            bgra->resize(need);
            const std::uint8_t* y_plane = src.Data();
            const std::uint8_t* u_plane = y_plane + y_bytes;
            const std::uint8_t* v_plane = u_plane + uv_plane_bytes;
            YuvToBgraScalar(y_plane, static_cast<std::size_t>(w),
                            u_plane, v_plane,
                            static_cast<std::size_t>(w / 2),
                            /*uv_interleaved=*/false, bgra->data(), w, h);
            break;
        }
    }
    *width = w;
    *height = h;
    return true;
}

bool Service::Present(XER::UI::Rendering::Renderer& renderer,
                      const video::Frame& frame,
                      XER::UI::Layout::Rect dst, float opacity) {
    if (!ready_) {
        last_backend_ = Backend::kNone;
        return false;
    }
    if (frame.Format() == video::PixelFormat::BGRA8) {
        // Zero-copy fast path — the producer's buffer goes straight to
        // the transport, no normalize pass.
        return PresentBgra(renderer, frame.Data(), frame.Width(),
                           frame.Height(), dst, opacity);
    }
    int w = 0, h = 0;
    if (!Normalize(frame, &scratch_, &w, &h)) {
        last_backend_ = Backend::kNone;
        return false;
    }
    return PresentBgra(renderer, scratch_.data(), w, h, dst, opacity);
}

bool Service::PresentBgra(XER::UI::Rendering::Renderer& renderer,
                          const void* bgra, int width, int height,
                          XER::UI::Layout::Rect dst, float opacity) {
    if (!ready_ || bgra == nullptr || width <= 0 || height <= 0) {
        last_backend_ = Backend::kNone;
        return false;
    }
#if XER_FR_HAS_RENDER_STORE
    if (options_.store != nullptr && options_.allow_store_upload) {
        const std::uint32_t payload = static_cast<std::uint32_t>(
            static_cast<std::size_t>(width) * height * 4u);
        // The write is seqlocked — a mid-write read on the worker side
        // just retries, so this can run inside the paint callback.
        if (options_.store->Write(
                options_.video_slot,
                XER::UI::Rendering::RenderStore::Kind::kVideoFrame,
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height), bgra, payload) &&
            renderer.DrawVideoFrame(options_.video_slot, dst, opacity,
                                    0.0f)) {
            // StreamRenderer serializes the id draw; the worker
            // resolves the pixels from the store we just wrote. A
            // renderer that returns false here (in-process backends)
            // falls through to the pixel path — the store write is
            // then wasted but harmless.
            last_backend_ = Backend::kRenderStore;
            return true;
        }
    }
#endif
    // In-process transport: DrawVideoPixels defaults to DrawFrameBgra
    // on the base interface; D2D/Vulkan backends stage the upload.
    if (renderer.DrawVideoPixels(bgra, options_.video_slot, width,
                                 height, dst, opacity, 0.0f)) {
        last_backend_ = Backend::kCpu;
        return true;
    }
    if (renderer.DrawFrameBgra(bgra, width, height, dst, opacity)) {
        last_backend_ = Backend::kCpu;
        return true;
    }
    last_backend_ = Backend::kNone;
    return false;
}

}  // namespace XER::FrameRender
