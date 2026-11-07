#include "canvas.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#ifdef DrawText
#undef DrawText
#endif

#include <core/SkCanvas.h>
#include <core/SkColor.h>
#include <core/SkColorSpace.h>
#include <core/SkData.h>
#include <core/SkFont.h>
#include <core/SkFontTypes.h>
#include <core/SkImage.h>
#include <core/SkPaint.h>
#include <core/SkPath.h>
#include <core/SkPathBuilder.h>
#include <core/SkPixmap.h>
#include <core/SkRect.h>
#include <core/SkSamplingOptions.h>
#include <core/SkSurface.h>
#include <core/SkTextBlob.h>
#include <effects/SkDashPathEffect.h>
#include <core/SkFontMgr.h>
#include <core/SkTypeface.h>
#include <ports/SkTypeface_win.h>
// Ganesh D3D12 backend — GPU-accelerated path for the canvas surface.
// Falls back to the raster surface automatically when unavailable.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <gpu/ganesh/d3d/GrD3DBackendContext.h>
#include <gpu/ganesh/d3d/GrD3DBackendSurface.h>
#include <gpu/ganesh/d3d/GrD3DDirectContext.h>
#include <gpu/ganesh/d3d/GrD3DTypes.h>
#include <gpu/ganesh/GrBackendSurface.h>
#include <gpu/ganesh/GrDirectContext.h>
#include <gpu/ganesh/SkSurfaceGanesh.h>
#endif

#include <cstdio>
#include <cstring>
#include <vector>

namespace XER::UI::Canvas {

#ifdef _WIN32
namespace {

const wchar_t kCanvasClassName[] = L"XERCanvasWindow";

class CanvasWindow {
public:
    static bool RegisterClass() {
        static bool registered = false;
        if (registered) {
            return true;
        }
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            return static_cast<LRESULT>(Canvas::WndProcTrampoline(hwnd, msg, wParam, lParam));
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 24));
        wc.lpszClassName = kCanvasClassName;
        registered = RegisterClassExW(&wc) != 0;
        return registered;
    }
};

// Temporary crash triage: step markers survive a hard crash, pinpointing
// the last completed GPU-init stage. Written to %TEMP%/XER/gpu_trace.log.
void GpuTrace(const char* step) {
    char path[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return;
    strncat_s(path, "XER\\gpu_trace.log", MAX_PATH - strlen(path) - 1);
    if (FILE* f = fopen(path, "ab")) {
        fprintf(f, "%llu %s\n",
                static_cast<unsigned long long>(GetTickCount64()), step);
        fclose(f);
    }
}
bool g_gpu_init_scope = false;  // trace allocator calls only during init

// Minimal GrD3DMemoryAllocator — committed resources only.
//
// Why: Skia's built-in fallback (GrD3DAMDMemoryAllocator/D3D12MA) returns
// nullptr intermittently on some drivers, and GrD3DGpu::Make then yields a
// null GPU that GrDirectContextPriv::Init dereferences — a crash inside
// MakeD3D. Supplying our own allocator removes that path entirely.
// createAliasingResource returns nullptr, which callers handle gracefully
// (MakeAliasingTexture falls back); aliasing is only a reuse optimization.
class GaneshD3DAllocator final : public GrD3DMemoryAllocator {
public:
    explicit GaneshD3DAllocator(ID3D12Device* device) {
        if (device != nullptr) device->AddRef();
        fDevice = gr_cp<ID3D12Device>(device);  // adopts the AddRef
    }

    gr_cp<ID3D12Resource> createResource(
        D3D12_HEAP_TYPE heapType, const D3D12_RESOURCE_DESC* desc,
        D3D12_RESOURCE_STATES initialState, sk_sp<GrD3DAlloc>* allocation,
        const D3D12_CLEAR_VALUE* clearValue) override {
        if (g_gpu_init_scope) GpuTrace("alloc.createResource enter");
        if (fDevice.get() == nullptr || desc == nullptr) {
            return gr_cp<ID3D12Resource>();
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = heapType;
        Microsoft::WRL::ComPtr<ID3D12Resource> raw;
        if (FAILED(fDevice->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, desc, initialState,
                clearValue, IID_PPV_ARGS(&raw))) || raw == nullptr) {
            return gr_cp<ID3D12Resource>();
        }
        if (allocation != nullptr) {
            *allocation = sk_sp<GrD3DAlloc>(new Alloc(raw.Get()));
        }
        raw->AddRef();  // gr_cp adopts without AddRef
        if (g_gpu_init_scope) GpuTrace("alloc.createResource ok");
        return gr_cp<ID3D12Resource>(raw.Get());
    }

    gr_cp<ID3D12Resource> createAliasingResource(
        sk_sp<GrD3DAlloc>&, uint64_t,
        const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
        const D3D12_CLEAR_VALUE*) override {
        if (g_gpu_init_scope) GpuTrace("alloc.createAliasingResource");
        return gr_cp<ID3D12Resource>();  // aliasing unsupported — safe fallback
    }

private:
    class Alloc final : public GrD3DAlloc {
    public:
        explicit Alloc(ID3D12Resource* res) {
            if (res != nullptr) res->AddRef();
            fResource = gr_cp<ID3D12Resource>(res);
        }
    private:
        gr_cp<ID3D12Resource> fResource;
    };

    gr_cp<ID3D12Device> fDevice;
};

SkColor ToSk(Canvas::Color c) {
    return SkColorSetARGB(
        static_cast<uint8_t>(c.a * 255.0f),
        static_cast<uint8_t>(c.r * 255.0f),
        static_cast<uint8_t>(c.g * 255.0f),
        static_cast<uint8_t>(c.b * 255.0f));
}

} // namespace

struct Canvas::Impl {
    sk_sp<SkSurface> surface;
    // Non-null only while the render callback executes.
    SkCanvas* active_canvas = nullptr;
    SkPaint paint;
    // Physical surface size (WM_SIZE pixels) and DIP->pixel factor. Widget
    // layout runs in DIPs (96/dpi scaling), so all public coordinates are
    // DIPs; the Skia canvas is scaled by dpi_scale_ during the callback.
    int surf_w_ = 0;
    int surf_h_ = 0;
    float dpi_scale_ = 1.0f;
    // Save-counts for nested PushClip calls (Skia canvas state stack).
    std::vector<int> clip_stack_;
    // Last physical-pixel rect passed to MoveWindow — SetBounds skips the
    // call entirely when nothing changed, avoiding WM_SIZE/surface-reset
    // churn (a real flicker source during animated layouts).
    int last_px_ = -1, last_py_ = -1, last_pw_ = -1, last_ph_ = -1;
    // Color the surface is cleared with on creation; Canvas::Clear keeps
    // this updated so a freshly allocated surface never shows garbage for
    // the frame between WM_SIZE and WM_PAINT.
    Color last_clear_{0.05f, 0.05f, 0.05f, 1.0f};

    // --- GPU path (Ganesh over D3D12) -------------------------------------
    // Created lazily on the first WM_PAINT; any init failure disables the
    // path permanently and the raster surface keeps working as fallback.
    struct Gpu {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swap;
        sk_sp<GrDirectContext> dctx;
        int width = 0;
        int height = 0;
    };
    std::unique_ptr<Gpu> gpu;
    bool gpu_tried = false;
    bool gpu_failed = false;
    // Persistent GPU staging surface for DrawImageBgra — video frames are
    // uploaded once per frame into a reused texture instead of allocating
    // a fresh SkData + raster image (and Ganesh texture) every call.
    sk_sp<SkSurface> image_surf;
    int image_surf_w = 0;
    int image_surf_h = 0;

    bool TryInitGpu(HWND hwnd);
    bool PaintGpu(Canvas* owner, HWND hwnd);
    // Runs render_callback_ into the given surface with the same canvas
    // state handling WM_PAINT uses for the raster path.
    void RenderInto(SkSurface* surf, Canvas* owner);

    void EnsureSurface(int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        if (surface == nullptr || surface->width() != w || surface->height() != h) {
            surface = SkSurfaces::Raster(
                SkImageInfo::Make(w, h, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
            // Fresh surfaces are uninitialized; pre-fill with the last clear
            // color so a resize never exposes a garbage frame.
            if (surface != nullptr) {
                surface->getCanvas()->clear(ToSk(last_clear_));
            }
        }
    }

    LRESULT HandleMessage(Canvas* owner, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

intptr_t Canvas::WndProcTrampoline(void* hwnd_ptr, unsigned int msg,
                                  intptr_t wparam, intptr_t lparam) {
    const HWND hwnd = reinterpret_cast<HWND>(hwnd_ptr);
    Canvas* self = reinterpret_cast<Canvas*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam),
                              static_cast<LPARAM>(lparam));
    }
    return self->impl_->HandleMessage(
        self, hwnd, msg, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

static void BlitSurfaceToWindow(HWND hwnd, SkSurface* surface) {
    if (surface == nullptr) {
        return;
    }
    SkPixmap pm;
    if (!surface->peekPixels(&pm) || pm.colorType() != kBGRA_8888_SkColorType) {
        return;
    }
    const int w = pm.width();
    const int h = pm.height();
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down: Skia rows are already top-first
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return;
    }
    StretchDIBits(dc, 0, 0, w, h, 0, 0, w, h,
                  pm.addr(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

LRESULT Canvas::Impl::HandleMessage(Canvas* owner, HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        const UINT w = LOWORD(lParam);
        const UINT h = HIWORD(lParam);
        surf_w_ = static_cast<int>(w);
        surf_h_ = static_cast<int>(h);
        owner->width_ = static_cast<int>(w / dpi_scale_ + 0.5f);
        owner->height_ = static_cast<int>(h / dpi_scale_ + 0.5f);
        surface.reset();
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_scale_ = static_cast<float>(HIWORD(wParam)) / 96.0f;
        if (lParam != 0) {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        // GPU path first — falls back to the raster surface when the
        // D3D12/Ganesh init failed or the frame could not be presented.
        if (!PaintGpu(owner, hwnd)) {
            EnsureSurface(surf_w_, surf_h_);
            if (surface != nullptr && owner->render_callback_) {
                RenderInto(surface.get(), owner);
                BlitSurfaceToWindow(hwnd, surface.get());
            } else {
                ValidateRect(hwnd, nullptr);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_LBUTTONDBLCLK:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR: {
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetFocus(hwnd);
            SetCapture(hwnd);
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            break;
        default:
            break;
        }
        if (owner->input_callback_) {
            int button = 0;
            int wheel = 0;
            int key = 0;
            switch (msg) {
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: button = 0; break;
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: button = 1; break;
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: button = 2; break;
            case WM_MOUSEMOVE:
                button = (wParam & MK_LBUTTON) ? 0 :
                         (wParam & MK_RBUTTON) ? 1 :
                         (wParam & MK_MBUTTON) ? 2 : -1;
                break;
            case WM_MOUSEWHEEL:
                wheel = GET_WHEEL_DELTA_WPARAM(wParam);
                break;
            case WM_LBUTTONDBLCLK:
                button = 0;
                break;
            case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
                key = static_cast<int>(wParam);
                break;
            default: break;
            }
            int mods = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= 1;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 2;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= 4;
            const float scale = dpi_scale_;
            const int cx = static_cast<int>(static_cast<short>(LOWORD(lParam)) / scale);
            const int cy = static_cast<int>(static_cast<short>(HIWORD(lParam)) / scale);
            if (owner->input_callback_(msg, cx, cy, button, wheel, key, mods)) {
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        // Release Ganesh/D3D12 objects while the window is still valid —
        // covers parent-initiated destruction that bypasses Canvas::Destroy.
        gpu.reset();
        image_surf.reset();
        image_surf_w = image_surf_h = 0;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool Canvas::Impl::TryInitGpu(HWND hwnd) {
    if (gpu != nullptr || gpu_failed) {
        return gpu != nullptr;
    }
    GpuTrace("TryInitGpu enter");
    auto g = std::make_unique<Gpu>();
    // IDXGIFactory6 -> pick the high-performance (non-CPU) adapter.
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory2))) ||
        FAILED(factory2->QueryInterface(IID_PPV_ARGS(&g->factory)))) {
        gpu_failed = true;
        return false;
    }
    GpuTrace("factory ok");
    if (FAILED(g->factory->EnumAdapterByGpuPreference(
            0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&g->adapter)))) {
        gpu_failed = true;
        return false;
    }
    GpuTrace("adapter ok");
    if (FAILED(D3D12CreateDevice(g->adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&g->device)))) {
        gpu_failed = true;
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    GpuTrace("device ok");
    if (FAILED(g->device->CreateCommandQueue(
            &qd, IID_PPV_ARGS(&g->queue)))) {
        gpu_failed = true;
        return false;
    }
    // gr_cp adopts a reference without AddRef — hand it one of our own so
    // the ComPtrs and the Ganesh context each own a separate reference.
    auto adopt = [](auto* p) {
        if (p != nullptr) p->AddRef();
        return p;
    };
    GpuTrace("queue ok");
    GrD3DBackendContext bctx;
    bctx.fAdapter = gr_cp<IDXGIAdapter1>(adopt(g->adapter.Get()));
    bctx.fDevice = gr_cp<ID3D12Device>(adopt(g->device.Get()));
    bctx.fQueue = gr_cp<ID3D12CommandQueue>(adopt(g->queue.Get()));
    // Our allocator — see GaneshD3DAllocator for why the built-in one is
    // not trusted here.
    bctx.fMemoryAllocator = sk_sp<GrD3DMemoryAllocator>(
        new GaneshD3DAllocator(g->device.Get()));
    GpuTrace("MakeD3D enter");
    g_gpu_init_scope = true;
    g->dctx = GrDirectContexts::MakeD3D(bctx);
    g_gpu_init_scope = false;
    GpuTrace(g->dctx != nullptr ? "MakeD3D ok" : "MakeD3D null");
    if (g->dctx == nullptr) {
        gpu_failed = true;
        return false;
    }
    // Flip-model swapchain bound to this canvas HWND. 2 buffers is the
    // minimum for FLIP_DISCARD.
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = static_cast<UINT>(std::max(1, surf_w_));
    sd.Height = static_cast<UINT>(std::max(1, surf_h_));
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    GpuTrace("swapchain create");
    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(g->factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)) ||
        FAILED(g->factory->CreateSwapChainForHwnd(
            g->queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1)) ||
        FAILED(sc1->QueryInterface(IID_PPV_ARGS(&g->swap)))) {
        gpu_failed = true;
        return false;
    }
    g->width = surf_w_;
    g->height = surf_h_;
    gpu = std::move(g);
    GpuTrace("TryInitGpu done");
    return true;
}

void Canvas::Impl::RenderInto(SkSurface* surf, Canvas* owner) {
    if (surf == nullptr || owner->render_callback_ == nullptr) {
        return;
    }
    active_canvas = surf->getCanvas();
    const int save = active_canvas->save();
    active_canvas->scale(dpi_scale_, dpi_scale_);
    owner->render_callback_(*owner);
    // Defensive: unbalanced PushClip calls must not leak canvas
    // state into the next frame.
    while (!clip_stack_.empty()) {
        if (clip_stack_.back() >= 0) {
            active_canvas->restoreToCount(clip_stack_.back());
        }
        clip_stack_.pop_back();
    }
    active_canvas->restoreToCount(save);
    active_canvas = nullptr;
}

bool Canvas::Impl::PaintGpu(Canvas* owner, HWND hwnd) {
    if (gpu_failed || surf_w_ <= 0 || surf_h_ <= 0) {
        return false;
    }
    if (gpu == nullptr && !gpu_tried) {
        gpu_tried = true;
        TryInitGpu(hwnd);
        std::fprintf(stderr, "[canvas] gpu init: %s\n",
                     gpu != nullptr ? "D3D12+Ganesh OK" : "FAILED -> raster");
    }
    if (gpu == nullptr || gpu->dctx == nullptr || gpu->swap == nullptr ||
        owner->render_callback_ == nullptr) {
        return false;
    }
    if (gpu->width != surf_w_ || gpu->height != surf_h_) {
        if (FAILED(gpu->swap->ResizeBuffers(
                0, static_cast<UINT>(surf_w_), static_cast<UINT>(surf_h_),
                DXGI_FORMAT_UNKNOWN, 0))) {
            gpu.reset();
            gpu_failed = true;
            return false;
        }
        gpu->width = surf_w_;
        gpu->height = surf_h_;
    }
    // A removed device makes every D3D12 call fail and can take Ganesh
    // down mid-frame — bail to the raster path before touching it.
    if (gpu->device != nullptr &&
        gpu->device->GetDeviceRemovedReason() != S_OK) {
        gpu_failed = true;
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
    const UINT index = gpu->swap->GetCurrentBackBufferIndex();
    if (FAILED(gpu->swap->GetBuffer(index, IID_PPV_ARGS(&backbuffer))) ||
        backbuffer == nullptr) {
        gpu_failed = true;
        return false;
    }
    // Wrap the current swapchain backbuffer; Ganesh transitions the
    // resource state (PRESENT -> RENDER_TARGET -> PRESENT) itself.
    GrD3DTextureResourceInfo info(backbuffer.Get(), sk_sp<GrD3DAlloc>(),
                                  D3D12_RESOURCE_STATE_PRESENT,
                                  DXGI_FORMAT_B8G8R8A8_UNORM, 1, 1, 0);
    const auto rt = GrBackendRenderTargets::MakeD3D(surf_w_, surf_h_, info);
    auto frame = SkSurfaces::WrapBackendRenderTarget(
        gpu->dctx.get(), rt, kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType, nullptr, nullptr);
    if (frame == nullptr) {
        return false;
    }
    RenderInto(frame.get(), owner);
    // Flush all pending Ganesh work and submit on the shared D3D12 queue —
    // ordering vs. Present is guaranteed by the queue/DXGI.
    gpu->dctx->flushAndSubmit(GrSyncCpu::kNo);
    if (FAILED(gpu->swap->Present(1, 0))) {
        // Device removed/reset (TDR, driver update, sleep/resume) — drop
        // to the raster path. `gpu` is NOT reset here: `frame` still holds
        // a GrDirectContext reference whose destructor must run while the
        // queue/device ComPtrs are alive; they die together in ~Gpu.
        gpu_failed = true;
        return false;
    }
    return true;
}

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}

Canvas::~Canvas() {
    Destroy();
}

bool Canvas::Create(void* parent_hwnd) {
    if (hwnd_ != nullptr || parent_hwnd == nullptr) {
        return false;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // S_FALSE is fine
    if (!CanvasWindow::RegisterClass()) {
        return false;
    }
    hwnd_ = CreateWindowExW(
        0, kCanvasClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 64, 64, static_cast<HWND>(parent_hwnd), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }
    SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    impl_->dpi_scale_ = static_cast<float>(GetDpiForWindow(static_cast<HWND>(hwnd_))) / 96.0f;
    // Pin the canvas to the bottom of the sibling z-order so plugin editor
    // HWNDs and other floating children always render above the graph.
    SetWindowPos(static_cast<HWND>(hwnd_), HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

void Canvas::PushClip(Rect rect) {
    if (impl_->active_canvas == nullptr) {
        impl_->clip_stack_.push_back(-1);
        return;
    }
    const int save = impl_->active_canvas->save();
    impl_->clip_stack_.push_back(save);
    impl_->active_canvas->clipRect(
        SkRect::MakeLTRB(rect.x, rect.y, rect.x + rect.w,
                         rect.y + rect.h),
        SkClipOp::kIntersect, true);
}

void Canvas::PopClip() {
    if (impl_->clip_stack_.empty()) {
        return;
    }
    const int save = impl_->clip_stack_.back();
    impl_->clip_stack_.pop_back();
    if (save >= 0 && impl_->active_canvas != nullptr) {
        impl_->active_canvas->restoreToCount(save);
    }
}

void Canvas::Destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    impl_->surface.reset();
    // GPU objects must die after any surface that referenced them.
    impl_->gpu.reset();
    impl_->image_surf.reset();
    impl_->image_surf_w = impl_->image_surf_h = 0;
    impl_->active_canvas = nullptr;
    impl_->clip_stack_.clear();
}

void Canvas::SetBounds(float x, float y, float w, float h) {
    if (hwnd_ == nullptr) {
        return;
    }
    impl_->dpi_scale_ = static_cast<float>(GetDpiForWindow(static_cast<HWND>(hwnd_))) / 96.0f;
    const float s = impl_->dpi_scale_;
    const int px = static_cast<int>(x * s);
    const int py = static_cast<int>(y * s);
    const int iw = std::max(1, static_cast<int>(w * s));
    const int ih = std::max(1, static_cast<int>(h * s));
    // Skip the move entirely when the pixel rect is unchanged — otherwise
    // every layout pass would fire WM_SIZE and reset the render surface.
    if (px == impl_->last_px_ && py == impl_->last_py_ &&
        iw == impl_->last_pw_ && ih == impl_->last_ph_) {
        return;
    }
    impl_->last_px_ = px;
    impl_->last_py_ = py;
    impl_->last_pw_ = iw;
    impl_->last_ph_ = ih;
    MoveWindow(static_cast<HWND>(hwnd_), px, py, iw, ih, TRUE);
}

void Canvas::GetBounds(float& x, float& y, float& w, float& h) const {
    if (hwnd_ == nullptr) {
        x = y = w = h = 0.0f;
        return;
    }
    RECT rc{};
    GetWindowRect(static_cast<HWND>(hwnd_), &rc);
    POINT tl{rc.left, rc.top};
    ScreenToClient(GetParent(static_cast<HWND>(hwnd_)), &tl);
    const float s = impl_->dpi_scale_ > 0.0f ? impl_->dpi_scale_ : 1.0f;
    x = static_cast<float>(tl.x) / s;
    y = static_cast<float>(tl.y) / s;
    w = static_cast<float>(rc.right - rc.left) / s;
    h = static_cast<float>(rc.bottom - rc.top) / s;
}

void Canvas::SetRenderCallback(std::function<void(Canvas&)> callback) {
    render_callback_ = std::move(callback);
}

void Canvas::SetInputCallback(InputCallback callback) {
    input_callback_ = std::move(callback);
}

void Canvas::SetVisible(bool visible) {
    if (hwnd_ != nullptr) {
        ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
    }
}

void Canvas::Invalidate() {
    if (hwnd_ != nullptr) {
        InvalidateRect(static_cast<HWND>(hwnd_), nullptr, FALSE);
    }
}

void Canvas::SetZOrderBottom() {
    if (hwnd_ != nullptr) {
        SetWindowPos(static_cast<HWND>(hwnd_), HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Canvas::Clear(Color color) {
    impl_->last_clear_ = color;
    if (impl_->active_canvas != nullptr) {
        impl_->active_canvas->clear(ToSk(color));
    }
}

void Canvas::FillRect(Rect rect, Color color, float radius) {
    if (impl_->active_canvas == nullptr) {
        return;
    }
    SkPaint& p = impl_->paint;
    p.reset();
    p.setAntiAlias(radius > 0.0f);
    p.setColor(ToSk(color));
    const SkRect r = SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h);
    if (radius > 0.0f) {
        impl_->active_canvas->drawRoundRect(r, radius, radius, p);
    } else {
        impl_->active_canvas->drawRect(r, p);
    }
}

void Canvas::StrokeRect(Rect rect, Color color, float width, float radius) {
    if (impl_->active_canvas == nullptr) {
        return;
    }
    SkPaint& p = impl_->paint;
    p.reset();
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(width);
    p.setColor(ToSk(color));
    const SkRect r = SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h);
    if (radius > 0.0f) {
        impl_->active_canvas->drawRoundRect(r, radius, radius, p);
    } else {
        impl_->active_canvas->drawRect(r, p);
    }
}

void Canvas::DrawLine(float x1, float y1, float x2, float y2, Color color, float width) {
    if (impl_->active_canvas == nullptr) {
        return;
    }
    SkPaint& p = impl_->paint;
    p.reset();
    p.setAntiAlias(true);
    p.setStrokeWidth(width);
    p.setColor(ToSk(color));
    impl_->active_canvas->drawLine(x1, y1, x2, y2, p);
}

void Canvas::DrawImageBgra(int width, int height, const void* bgra_pixels) {
    if (impl_->active_canvas == nullptr || bgra_pixels == nullptr ||
        width <= 0 || height <= 0) {
        return;
    }
    // GPU path: blit into a persistent Ganesh surface (one CPU->GPU
    // upload per frame, no per-frame SkData/image/texture allocation),
    // then draw its snapshot. Falls through to the raster path when the
    // canvas is not GPU-backed.
    if (impl_->gpu != nullptr && impl_->gpu->dctx != nullptr) {
        if (impl_->image_surf == nullptr || impl_->image_surf_w != width ||
            impl_->image_surf_h != height) {
            const SkImageInfo info = SkImageInfo::Make(
                width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
            impl_->image_surf = SkSurfaces::RenderTarget(
                impl_->gpu->dctx.get(), skgpu::Budgeted::kNo,
                info);
            impl_->image_surf_w = impl_->image_surf != nullptr ? width : 0;
            impl_->image_surf_h = impl_->image_surf != nullptr ? height : 0;
        }
        if (impl_->image_surf != nullptr) {
            const SkImageInfo info = SkImageInfo::Make(
                width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
            SkPixmap pm(info, bgra_pixels, width * 4);
            impl_->image_surf->writePixels(pm, 0, 0);
            sk_sp<SkImage> snap = impl_->image_surf->makeImageSnapshot();
            if (snap != nullptr) {
                impl_->active_canvas->drawImageRect(
                    snap.get(),
                    SkRect::MakeWH(static_cast<SkScalar>(width),
                                   static_cast<SkScalar>(height)),
                    SkRect::MakeWH(static_cast<SkScalar>(GetWidth()),
                                   static_cast<SkScalar>(GetHeight())),
                    SkSamplingOptions(SkFilterMode::kLinear),
                    nullptr,
                    SkCanvas::kFast_SrcRectConstraint);
                return;
            }
        }
    }
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    sk_sp<SkData> data = SkData::MakeWithCopy(bgra_pixels, bytes);
    if (data == nullptr) {
        return;
    }
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, width * 4);
    if (image == nullptr) {
        return;
    }
    impl_->active_canvas->drawImageRect(
        image.get(),
        SkRect::MakeWH(static_cast<SkScalar>(width), static_cast<SkScalar>(height)),
        SkRect::MakeWH(static_cast<SkScalar>(GetWidth()), static_cast<SkScalar>(GetHeight())),
        SkSamplingOptions(SkFilterMode::kLinear),
        nullptr,
        SkCanvas::kFast_SrcRectConstraint);
}

void Canvas::DrawCubicBezier(float x1, float y1, float cx1, float cy1,
                             float cx2, float cy2, float x2, float y2,
                             Color color, float width,
                             float dash_on, float dash_off) {
    if (impl_->active_canvas == nullptr) {
        return;
    }
    SkPaint& p = impl_->paint;
    p.reset();
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(width);
    p.setColor(ToSk(color));
    if (dash_on > 0.0f) {
        const SkScalar intervals[2] = {dash_on, dash_off > 0.0f ? dash_off : dash_on};
        p.setPathEffect(SkDashPathEffect::Make(
            SkSpan<const SkScalar>(intervals, 2), 0.0f));
    }
    SkPathBuilder builder;
    builder.moveTo(x1, y1);
    builder.cubicTo(SkPoint::Make(cx1, cy1), SkPoint::Make(cx2, cy2),
                    SkPoint::Make(x2, y2));
    impl_->active_canvas->drawPath(builder.detach(), p);
}

void Canvas::FillCircle(float cx, float cy, float radius, Color color) {
    if (impl_->active_canvas == nullptr || radius <= 0.0f) {
        return;
    }
    SkPaint& p = impl_->paint;
    p.reset();
    p.setAntiAlias(true);
    p.setColor(ToSk(color));
    impl_->active_canvas->drawCircle(cx, cy, radius, p);
}

void Canvas::DrawText(std::string_view text, float x, float y,
                      float size_px, Color color) {
    if (impl_->active_canvas == nullptr || text.empty()) {
        return;
    }
    // A default-constructed SkFont has no typeface and draws nothing —
    // resolve a real typeface once via the GDI font manager and reuse it.
    static const sk_sp<SkTypeface> kTypeface = [] {
        sk_sp<SkFontMgr> mgr = SkFontMgr_New_GDI();
        if (mgr == nullptr) {
            mgr = SkFontMgr::RefEmpty();
        }
        sk_sp<SkTypeface> tf =
            mgr->legacyMakeTypeface(nullptr, SkFontStyle());
        if (tf == nullptr) {
            tf = mgr->matchFamilyStyle("Arial", SkFontStyle());
        }
        return tf;
    }();
    SkFont font;
    font.setTypeface(kTypeface);
    font.setSize(size_px);
    font.setEdging(SkFont::Edging::kAntiAlias);
    SkPaint paint;
    paint.setColor(ToSk(color));
    paint.setAntiAlias(true);
    impl_->active_canvas->drawString(
        SkString(std::string(text)), x, y, font, paint);
}

#else // !_WIN32

struct Canvas::Impl {};

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
Canvas::~Canvas() { Destroy(); }
bool Canvas::Create(void*) { return false; }
void Canvas::Destroy() {}
void Canvas::SetBounds(float, float, float, float) {}
void Canvas::GetBounds(float& x, float& y, float& w, float& h) const { x = y = w = h = 0.0f; }
void Canvas::SetRenderCallback(std::function<void(Canvas&)>) {}
void Canvas::SetInputCallback(InputCallback) {}
void Canvas::SetVisible(bool) {}
void Canvas::Invalidate() {}
void Canvas::SetZOrderBottom() {}
void Canvas::Clear(Color) {}
void Canvas::FillRect(Rect, Color, float) {}
void Canvas::StrokeRect(Rect, Color, float, float) {}
void Canvas::DrawLine(float, float, float, float, Color, float) {}
void Canvas::DrawImageBgra(int, int, const void*) {}
void Canvas::DrawCubicBezier(float, float, float, float, float, float,
                             float, float, Color, float) {}
void Canvas::DrawText(std::string_view, float, float, float, Color) {}

#endif

} // namespace XER::UI::Canvas
