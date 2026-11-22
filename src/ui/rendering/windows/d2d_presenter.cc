#include "d2d_presenter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace XER::UI::Rendering::Windows {
namespace {

constexpr DWORD kMutexAcquireTimeoutMs = 200;

DWORD WINAPI PresenterThreadProc(LPVOID param) {
    static_cast<D2DPresenter*>(param)->ThreadMain();
    return 0;
}

} // namespace

D2DPresenter::D2DPresenter(HWND hwnd) : hwnd_(hwnd) {
    thread_ = CreateThread(nullptr, 0, &PresenterThreadProc, this,
                           0, nullptr);
}

D2DPresenter::~D2DPresenter() {
    AcquireSRWLockExclusive(&job_lock_);
    stop_ = true;
    has_pending_ = true;
    WakeAllConditionVariable(&job_cv_);
    ReleaseSRWLockExclusive(&job_lock_);
    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
}

void D2DPresenter::Submit(const Frame& frame) {
    AcquireSRWLockExclusive(&job_lock_);
    Frame merged = frame;
    if (has_pending_) {
        ++dropped_;  // replaced before being presented — coalesced
        // The replaced frame's damage must still reach the screen: the
        // canvas already holds its pixels, but the backbuffer was never
        // updated. Fold its dirty rects into the replacement job —
        // overflowing the list degrades to a full copy, never a loss.
        if (pending_.dirty_count != 0 && merged.dirty_count != 0) {
            for (std::uint32_t i = 0; i < pending_.dirty_count; ++i) {
                if (merged.dirty_count >=
                    static_cast<std::uint32_t>(std::size(merged.dirty))) {
                    merged.dirty_count = 0;  // list full -> whole frame
                    break;
                }
                merged.dirty[merged.dirty_count++] = pending_.dirty[i];
            }
        } else {
            merged.dirty_count = 0;  // either side wanted a full present
        }
    }
    pending_ = merged;
    has_pending_ = true;
    WakeConditionVariable(&job_cv_);
    ReleaseSRWLockExclusive(&job_lock_);
}

bool D2DPresenter::EnsureDevice() {
    if (device_ != nullptr) {
        return true;
    }
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        kLevels, static_cast<UINT>(std::size(kLevels)),
        D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr,
        context_.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            kLevels, static_cast<UINT>(std::size(kLevels)),
            D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr,
            context_.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[d2d_presenter] D3D11CreateDevice failed 0x%lx\n",
                     static_cast<unsigned long>(hr));
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi1;
    if (SUCCEEDED(device_.CopyTo(dxgi1.GetAddressOf()))) {
        // The thread blocks in Present anyway when busy; a shallow queue
        // keeps displayed frames fresh.
        dxgi1->SetMaximumFrameLatency(2);
    }
    return true;
}

void D2DPresenter::DropDevice() {
    hud_format_.Reset();
    hud_write_.Reset();
    hud_brush_.Reset();
    hud_target_.Reset();
    hud_dc_.Reset();
    hud_d2d_device_.Reset();
    hud_d2d_factory_.Reset();
    shared_mutex_.Reset();
    shared_tex_.Reset();
    backbuffer_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device_.Reset();
    opened_generation_ = 0;
    swap_width_ = swap_height_ = 0;
    has_stale_ = false;
    backbuffer_dirty_ = true;
}

void D2DPresenter::EnsureSwapchain(std::uint32_t width,
                                   std::uint32_t height) {
    if (swap_chain_ != nullptr && swap_width_ == width &&
        swap_height_ == height) {
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }
    if (swap_chain_ != nullptr) {
        // All backbuffer references must be released before resize.
        if (hud_dc_ != nullptr) {
            hud_dc_->SetTarget(nullptr);
        }
        hud_target_.Reset();
        backbuffer_.Reset();
        const HRESULT hr = swap_chain_->ResizeBuffers(
            0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(hr)) {
            swap_width_ = width;
            swap_height_ = height;
            swap_chain_->GetBuffer(
                0, IID_PPV_ARGS(backbuffer_.GetAddressOf()));
            backbuffer_dirty_ = true;  // contents are now undefined
            return;
        }
        swap_chain_.Reset();
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device_.CopyTo(dxgi.GetAddressOf())) ||
        FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
        FAILED(adapter->GetParent(
            IID_PPV_ARGS(factory.GetAddressOf())))) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(
            device_.Get(), hwnd_, &desc, nullptr, nullptr,
            swap_chain_.GetAddressOf()))) {
        return;
    }
    factory->MakeWindowAssociation(
        hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    swap_width_ = width;
    swap_height_ = height;
    swap_chain_->GetBuffer(0, IID_PPV_ARGS(backbuffer_.GetAddressOf()));
    backbuffer_dirty_ = true;
}

void D2DPresenter::DrawHud(const Frame& frame) {
    // Lazily build the tiny D2D surface over device B. Drawn straight
    // onto the backbuffer after the canvas copy — transient by nature,
    // matching the old engine's HUD semantics.
    if (hud_d2d_factory_ == nullptr &&
        FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), nullptr,
            reinterpret_cast<void**>(
                hud_d2d_factory_.GetAddressOf())))) {
        return;
    }
    if (hud_write_ == nullptr &&
        FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(
                hud_write_.GetAddressOf())))) {
        return;
    }
    if (hud_format_ == nullptr &&
        FAILED(hud_write_->CreateTextFormat(
            L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"en-us", hud_format_.GetAddressOf()))) {
        return;
    }
    if (hud_d2d_device_ == nullptr) {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        if (FAILED(device_.CopyTo(dxgi.GetAddressOf())) ||
            FAILED(hud_d2d_factory_->CreateDevice(
                dxgi.Get(), hud_d2d_device_.GetAddressOf())) ||
            FAILED(hud_d2d_device_->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                hud_dc_.GetAddressOf()))) {
            return;
        }
    }
    if (hud_target_ == nullptr) {
        Microsoft::WRL::ComPtr<IDXGISurface> surface;
        if (FAILED(backbuffer_.CopyTo(surface.GetAddressOf())) ||
            FAILED(hud_dc_->CreateBitmapFromDxgiSurface(
                surface.Get(), nullptr,
                hud_target_.GetAddressOf()))) {
            return;
        }
    }
    if (hud_brush_ == nullptr &&
        FAILED(hud_dc_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f),
            hud_brush_.GetAddressOf()))) {
        return;
    }

    wchar_t wide[192];
    const int wlen = MultiByteToWideChar(
        CP_UTF8, 0, frame.hud_text, -1, wide,
        static_cast<int>(std::size(wide)));
    if (wlen <= 0) {
        return;
    }
    const D2D1_RECT_F box = D2D1::RectF(
        static_cast<float>(frame.hud_rect.left),
        static_cast<float>(frame.hud_rect.top),
        static_cast<float>(frame.hud_rect.right),
        static_cast<float>(frame.hud_rect.bottom));

    hud_dc_->BeginDraw();
    hud_dc_->SetTarget(hud_target_.Get());
    hud_brush_->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f));
    hud_dc_->FillRectangle(box, hud_brush_.Get());
    hud_brush_->SetColor(D2D1::ColorF(0.35f, 0.95f, 0.45f, 1.0f));
    hud_dc_->DrawText(
        wide, static_cast<UINT32>(wlen - 1), hud_format_.Get(), box,
        hud_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    hud_dc_->EndDraw();
    hud_dc_->SetTarget(nullptr);
}

bool D2DPresenter::OpenShared(const Frame& frame) {
    if (shared_tex_ != nullptr &&
        opened_generation_ == frame.generation) {
        return true;
    }
    shared_mutex_.Reset();
    shared_tex_.Reset();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device_->OpenSharedResource(
            frame.shared_handle, IID_PPV_ARGS(tex.GetAddressOf())))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
    if (FAILED(tex.CopyTo(mutex.GetAddressOf()))) {
        return false;
    }
    shared_tex_ = std::move(tex);
    shared_mutex_ = std::move(mutex);
    opened_generation_ = frame.generation;
    return true;
}

void D2DPresenter::ThreadMain() {
    for (;;) {
        Frame job{};
        {
            AcquireSRWLockExclusive(&job_lock_);
            while (!has_pending_) {
                SleepConditionVariableSRW(&job_cv_, &job_lock_,
                                          INFINITE, 0);
            }
            if (stop_) {
                ReleaseSRWLockExclusive(&job_lock_);
                break;
            }
            job = pending_;
            has_pending_ = false;
            ReleaseSRWLockExclusive(&job_lock_);
        }

        // A consumed job that never reaches Present still must not lose
        // its damage: fold the regions into the stale set carried into
        // the next successful present.
        auto fold_to_stale = [&](const Frame& f) {
            if (f.dirty_count == 0) {
                stale_dirty_ = RECT{0, 0,
                                    static_cast<LONG>(f.pixel_width),
                                    static_cast<LONG>(f.pixel_height)};
                has_stale_ = true;
                return;
            }
            if (!has_stale_) {
                stale_dirty_ = f.dirty[0];
                has_stale_ = true;
            }
            for (std::uint32_t i = 0; i < f.dirty_count; ++i) {
                const RECT& r = f.dirty[i];
                stale_dirty_.left = std::min(stale_dirty_.left, r.left);
                stale_dirty_.top = std::min(stale_dirty_.top, r.top);
                stale_dirty_.right =
                    std::max(stale_dirty_.right, r.right);
                stale_dirty_.bottom =
                    std::max(stale_dirty_.bottom, r.bottom);
            }
        };

        if (!EnsureDevice()) {
            fold_to_stale(job);
            Sleep(100);  // no device: retry next job
            continue;
        }
        EnsureSwapchain(job.pixel_width, job.pixel_height);
        if (swap_chain_ == nullptr || backbuffer_ == nullptr ||
            !OpenShared(job)) {
            fold_to_stale(job);
            continue;
        }

        // Acquire the read end of the producer mutex; blocks (GPU-ordered)
        // only until the engine's draw commands reach the release point.
        if (FAILED(shared_mutex_->AcquireSync(1, kMutexAcquireTimeoutMs))) {
            fold_to_stale(job);  // producer wedged — damage rides next job
            continue;
        }
        auto copy_region = [&](const RECT& r) {
            D3D11_BOX box{};
            box.left = static_cast<UINT>(std::max<LONG>(r.left, 0));
            box.top = static_cast<UINT>(std::max<LONG>(r.top, 0));
            box.right = static_cast<UINT>(r.right);
            box.bottom = static_cast<UINT>(r.bottom);
            box.back = 1;
            if (box.right <= box.left || box.bottom <= box.top) {
                return;
            }
            context_->CopySubresourceRegion(
                backbuffer_.Get(), 0, box.left, box.top, 0,
                shared_tex_.Get(), 0, &box);
        };
        const bool need_full =
            backbuffer_dirty_ || job.dirty_count == 0;
        if (!need_full) {
            for (std::uint32_t i = 0; i < job.dirty_count; ++i) {
                copy_region(job.dirty[i]);
            }
            // Fold any region left stale by a previously dropped
            // present — same mutex scope, pixels already in the canvas.
            if (has_stale_) {
                bool covered = false;
                for (std::uint32_t i = 0; i < job.dirty_count; ++i) {
                    const RECT& r = job.dirty[i];
                    if (r.left <= stale_dirty_.left &&
                        r.top <= stale_dirty_.top &&
                        r.right >= stale_dirty_.right &&
                        r.bottom >= stale_dirty_.bottom) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    copy_region(stale_dirty_);
                }
            }
        } else {
            context_->CopyResource(backbuffer_.Get(), shared_tex_.Get());
        }
        shared_mutex_->ReleaseSync(0);
        context_->Flush();
        // The copy just defined the whole backbuffer (full path) or the
        // touched regions (partial path): either way the buffer is no
        // longer undefined after a full copy.
        if (need_full) {
            backbuffer_dirty_ = false;
        }

        if (job.has_hud) {
            DrawHud(job);
        }

        HRESULT hr;
        if (!need_full) {
            RECT dirty[6];
            UINT n = 0;
            for (std::uint32_t i = 0; i < job.dirty_count && n < 4; ++i) {
                dirty[n++] = job.dirty[i];
            }
            if (has_stale_) {
                dirty[n++] = stale_dirty_;
            }
            // The HUD is drawn onto the backbuffer outside the canvas
            // copy — its rect must join the dirty list or DWM skips it
            // on partial presents.
            if (job.has_hud && n < 6) {
                dirty[n++] = job.hud_rect;
            }
            DXGI_PRESENT_PARAMETERS params{};
            params.DirtyRectsCount = n;
            params.pDirtyRects = dirty;
            hr = swap_chain_->Present1(0, 0, &params);
        } else {
            hr = swap_chain_->Present(0, 0);
        }

        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
            // Rare at latency 2, but carry the damage forward so the
            // pixels (already in the canvas) reach the screen with the
            // next present.
            fold_to_stale(job);
            continue;
        }
        if (SUCCEEDED(hr)) {
            has_stale_ = false;
            ++presented_;
            continue;
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED ||
            hr == DXGI_ERROR_DEVICE_RESET) {
            DropDevice();  // lazy rebuild on the next job
            continue;
        }
        // Other failure: drop the frame; the producer keeps painting.
    }
    DropDevice();
}

} // namespace XER::UI::Rendering::Windows
