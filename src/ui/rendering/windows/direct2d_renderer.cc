#include "direct2d_renderer.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#ifdef DrawText
#undef DrawText
#endif

namespace XER::UI::Rendering::Windows {
namespace {

void ThrowIfFailed(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) {
        return;
    }

    std::ostringstream message;
    message << operation << " failed with HRESULT 0x" << std::hex
            << static_cast<unsigned long>(result);
    throw std::runtime_error(message.str());
}

D2D1_RECT_F ToRect(Layout::Rect rect) {
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
}

Layout::Rect NormalizeRect(Layout::Rect rect) {
    // Widgets occasionally compute negative extents (overflowed columns,
    // empty tracks, undersized panels). Degenerate geometry must not
    // abort the whole paint pass — clamp to zero so the call becomes a
    // no-op instead of throwing mid-frame.
    rect.width = std::max(0.0f, rect.width);
    rect.height = std::max(0.0f, rect.height);
    return rect;
}

std::wstring ToWideString(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Text exceeds the supported UTF-8 length");
    }

    std::wstring wide = XER::Helper::String::Utf8ToWide(text);
    if (wide.empty() && !text.empty()) {
        throw std::runtime_error("Text is not valid UTF-8");
    }
    return wide;
}

std::string FormatKey(const TextStyle& style) {
    return std::to_string(style.size) + "|" + std::to_string(style.weight) +
           "|" + (style.monospace ? "m" : "p");
}

// Appends a float/int to a cache key without allocating temporaries
// (std::to_string allocates; std::to_chars writes into a stack buffer).
void AppendNumber(std::string& out, float value) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, r.ptr);
}
void AppendNumber(std::string& out, int value) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, r.ptr);
}

// Same byte layout as FormatKey(), written into `out` without the
// intermediate std::string.
void AppendStyleKey(std::string& out, const TextStyle& style) {
    AppendNumber(out, style.size);
    out.push_back('|');
    AppendNumber(out, style.weight);
    out.push_back('|');
    out.push_back(style.monospace ? 'm' : 'p');
}

// LRU lookup: promotes the hit to the front of `order` (most recent).
// Map value types carry a `lru_it` back-pointer into `order`.
template <typename Map>
typename Map::mapped_type::value_type* LruGet(
    Map& map, std::list<std::string>& order, const std::string& key) {
    const auto it = map.find(key);
    if (it == map.end()) {
        return nullptr;
    }
    order.splice(order.begin(), order, it->second.lru_it);
    return &it->second.value;
}

// LRU insert; evicts the least-recently-used entry once `cap` is exceeded
// (bounded eviction keeps hot entries — unlike the old clear-all cap).
template <typename Map>
void LruPut(Map& map, std::list<std::string>& order, const std::string& key,
            typename Map::mapped_type::value_type value, std::size_t cap) {
    order.push_front(key);
    map.emplace(
        key,
        typename Map::mapped_type{std::move(value), order.begin()});
    while (map.size() > cap) {
        map.erase(order.back());
        order.pop_back();
    }
}

} // namespace

Direct2DRenderer::Direct2DRenderer(HWND hwnd)
    : hwnd_(hwnd)
    , dpi_(hwnd != nullptr ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI) {
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }
    // Escape hatch for A/B testing the dirty-rect present path.
    if (const char* env = std::getenv("XER_UI_PRESENT_FULL")) {
        force_full_present_ =
            env[0] != '\0' &&
            !(env[0] == '0' && env[1] == '\0') &&
            !(env[0] == 'o' && env[1] == 'f' && env[2] == 'f');
    }
    ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())
        ),
        "DWriteCreateFactory"
    );
    // Factory must outlive the D2D device so geometries created for
    // DrawImage rounded-clip masks share the device context's factory.
    ThrowIfFailed(
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),
            nullptr,
            reinterpret_cast<void**>(d2d_factory_.GetAddressOf())),
        "D2D1CreateFactory"
    );
}

Direct2DRenderer::~Direct2DRenderer() = default;

void Direct2DRenderer::SetClearColor(Theme::Color color) {
    clear_color_ = color;
}

void Direct2DRenderer::SetDpi(UINT dpi) {
    if (dpi == 0) {
        throw std::invalid_argument("DPI must be greater than zero");
    }
    if (dpi_ == dpi) {
        return;
    }
    if (frame_started_) {
        throw std::logic_error("Cannot change renderer DPI during an active frame");
    }

    dpi_ = dpi;
    if (dc_ != nullptr) {
        dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    }
    // Canvas bitmap and backbuffer target bake DPI into their properties.
    ReleaseTargets();
}

void Direct2DRenderer::EnsureDevice() {
    if (d3d_device_ != nullptr && dc_ != nullptr) {
        return;
    }
    if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
        throw std::runtime_error("Cannot create a renderer for an invalid HWND");
    }

    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        kLevels, static_cast<UINT>(std::size(kLevels)),
        D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
        nullptr, nullptr);
    if (FAILED(hr)) {
        // WARP keeps the UI functional on machines without a usable GPU.
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            kLevels, static_cast<UINT>(std::size(kLevels)),
            D3D11_SDK_VERSION, d3d_device_.GetAddressOf(),
            nullptr, nullptr);
    }
    ThrowIfFailed(hr, "D3D11CreateDevice");

    IDXGIDevice* raw_dxgi = nullptr;
    ThrowIfFailed(
        d3d_device_->QueryInterface(IID_PPV_ARGS(&raw_dxgi)),
        "ID3D11Device::QueryInterface(IDXGIDevice)");
    dxgi_device_.Attach(raw_dxgi);

    // The device's DXGI parent is not guaranteed to be an IDXGIFactory2;
    // create one explicitly — it is required for flip-model swapchains.
    ThrowIfFailed(
        CreateDXGIFactory1(IID_PPV_ARGS(dxgi_factory_.GetAddressOf())),
        "CreateDXGIFactory1");

    if (auto* dev1 = dxgi_device_.Get()) {
        Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi1;
        if (SUCCEEDED(dev1->QueryInterface(IID_PPV_ARGS(&dxgi1)))) {
            dxgi1->SetMaximumFrameLatency(1);
        }
    }

    ThrowIfFailed(
        d2d_factory_->CreateDevice(dxgi_device_.Get(),
                                   d2d_device_.GetAddressOf()),
        "ID2D1Factory1::CreateDevice");
    ThrowIfFailed(
        d2d_device_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.GetAddressOf()),
        "ID2D1Device::CreateDeviceContext");
    dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
}

void Direct2DRenderer::ReleaseTargets() {
    canvas_.Reset();
    backbuffer_.Reset();
    backbuffer_target_.Reset();
    canvas_valid_ = false;
    target_size_ = {0, 0};
}

void Direct2DRenderer::EnsureTargets(UINT32 width, UINT32 height) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("Cannot create a zero-size swapchain");
    }
    if (swap_chain_ == nullptr) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 0;   // derive from the HWND client area
        desc.Height = 0;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc = {1, 0};
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ThrowIfFailed(
            dxgi_factory_->CreateSwapChainForHwnd(
                d3d_device_.Get(), hwnd_, &desc,
                nullptr, nullptr, swap_chain_.GetAddressOf()),
            "IDXGIFactory2::CreateSwapChainForHwnd");
        // Keep DXGI from hijacking Alt+Enter / window management.
        dxgi_factory_->MakeWindowAssociation(
            hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        target_size_ = {0, 0};
    }

    if (target_size_.width == width && target_size_.height == height &&
        canvas_ != nullptr && backbuffer_target_ != nullptr) {
        return;
    }

    if (swap_chain_ != nullptr && target_size_.width != 0) {
        // DXGI requires every reference to the back buffers released
        // before ResizeBuffers — the D2D bitmap wrapping the surface, the
        // DC's active target binding, and the surface itself all count.
        dc_->SetTarget(nullptr);
        backbuffer_target_.Reset();
        backbuffer_.Reset();
        const HRESULT resize_hr = swap_chain_->ResizeBuffers(
            0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(resize_hr)) {
            ReleaseTargets();
            swap_chain_.Reset();
            ThrowIfFailed(resize_hr, "IDXGISwapChain::ResizeBuffers");
        }
    }

    Microsoft::WRL::ComPtr<IDXGISurface> backbuffer_surface;
    ThrowIfFailed(
        swap_chain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer_surface)),
        "IDXGISwapChain::GetBuffer");
    ThrowIfFailed(
        dc_->CreateBitmapFromDxgiSurface(
            backbuffer_surface.Get(), nullptr,
            backbuffer_target_.GetAddressOf()),
        "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    backbuffer_ = std::move(backbuffer_surface);

    ThrowIfFailed(
        dc_->CreateBitmap(
            D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET,
                D2D1_PIXEL_FORMAT(DXGI_FORMAT_B8G8R8A8_UNORM,
                                  D2D1_ALPHA_MODE_PREMULTIPLIED),
                static_cast<float>(dpi_), static_cast<float>(dpi_)),
            canvas_.GetAddressOf()),
        "ID2D1DeviceContext::CreateBitmap(canvas)");
    // A recreated canvas has no previous frame to retain.
    canvas_valid_ = false;
    targets_recreated_ = true;

    target_size_ = D2D1::SizeU(width, height);
}

void Direct2DRenderer::SetFrameDamage(Layout::Rect damage) {
    // Called by WidgetTree::Render before BeginFrame. Snapshot it there —
    // a frame that begins without a fresh hint gets no damage rect and
    // presents fully.
    pending_damage_ = damage;
    pending_damage_valid_ = damage.width > 0.0f && damage.height > 0.0f;
}

void Direct2DRenderer::BeginFrame(Layout::Rect bounds) {
    if (frame_started_) {
        throw std::logic_error("A Direct2D frame is already active");
    }
    if (bounds.width < 0.0f || bounds.height < 0.0f) {
        throw std::invalid_argument("Frame bounds cannot have negative dimensions");
    }

    counters_ = {};
    active_damage_ = pending_damage_;
    active_damage_valid_ = pending_damage_valid_;
    pending_damage_ = {};
    pending_damage_valid_ = false;

    targets_recreated_ = false;
    EnsureDevice();
    RECT client_rect{};
    if (!GetClientRect(hwnd_, &client_rect)) {
        throw std::runtime_error("GetClientRect failed while beginning a frame");
    }
    const UINT32 pixel_width =
        static_cast<UINT32>(std::max<LONG>(1, client_rect.right));
    const UINT32 pixel_height =
        static_cast<UINT32>(std::max<LONG>(1, client_rect.bottom));
    EnsureTargets(pixel_width, pixel_height);

    // The canvas is cleared only when it has no valid previous frame —
    // widget drawing is damage-culled upstream, but the final blit always
    // covers the whole canvas (see EndFrame).
    dc_->BeginDraw();
    dc_->SetTarget(canvas_.Get());
    if (!canvas_valid_) {
        dc_->Clear(ToColor(clear_color_));
    }
    frame_started_ = true;
}

void Direct2DRenderer::EndFrame() {
    RequireFrame();
    frame_started_ = false;

    // Damage -> pixel-space dirty rect. Full present when there is no
    // damage hint, when targets were rebuilt this frame (fresh swapchain
    // buffer contents are undefined), or when the hint already covers the
    // whole client area.
    const float dip_to_px = static_cast<float>(dpi_) / 96.0f;
    auto dip_to_px_rect = [&](const Layout::Rect& r) {
        RECT rc{
            static_cast<LONG>(std::floor(r.x * dip_to_px)) - 1,
            static_cast<LONG>(std::floor(r.y * dip_to_px)) - 1,
            static_cast<LONG>(std::ceil((r.x + r.width) * dip_to_px)) + 1,
            static_cast<LONG>(std::ceil((r.y + r.height) * dip_to_px)) + 1,
        };
        rc.left = std::max<LONG>(rc.left, 0);
        rc.top = std::max<LONG>(rc.top, 0);
        rc.right =
            std::min<LONG>(rc.right, static_cast<LONG>(target_size_.width));
        rc.bottom = std::min<LONG>(rc.bottom,
                                   static_cast<LONG>(target_size_.height));
        return rc;
    };

    RECT damage_px{};
    if (active_damage_valid_) {
        damage_px = dip_to_px_rect(active_damage_);
    }
    const bool damage_empty = !active_damage_valid_ ||
        damage_px.right <= damage_px.left ||
        damage_px.bottom <= damage_px.top;
    const bool damage_covers_all =
        active_damage_valid_ && damage_px.left <= 0 && damage_px.top <= 0 &&
        damage_px.right >= static_cast<LONG>(target_size_.width) &&
        damage_px.bottom >= static_cast<LONG>(target_size_.height);
    const bool full_present = force_full_present_ || targets_recreated_ ||
        !active_damage_valid_ || damage_covers_all;

    dc_->SetTarget(backbuffer_target_.Get());
    if (full_present) {
        // Whole retained canvas -> back buffer.
        dc_->DrawBitmap(
            canvas_.Get(),
            nullptr,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    } else if (!damage_empty) {
        // Flip-model + Present1 dirty rects: the runtime pulls the
        // untouched region forward from the previously presented frame,
        // so only the damaged subrect needs to land on the back buffer.
        // (Each buffer's stale content outside the rect is never shown.)
        const D2D1_RECT_F dr = ToRect(active_damage_);
        dc_->DrawBitmap(
            canvas_.Get(),
            &dr,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
            &dr);
    }

    Layout::Rect hud_box{};
    const bool hud_drawn = hud_enabled_ && DrawPerfHud(&hud_box);

    const auto result = dc_->EndDraw();
    if (FAILED(result)) {
        if (result == D2DERR_RECREATE_TARGET) {
            ReleaseTargets();
            return;  // next frame rebuilds targets; this frame is dropped
        }
        ThrowIfFailed(result, "ID2D1DeviceContext::EndDraw");
    }
    // Canvas now holds a complete frame.
    canvas_valid_ = true;

    HRESULT present_hr = S_OK;
    if (full_present) {
        present_hr = swap_chain_->Present(1, 0);
    } else {
        RECT dirty[2];
        UINT dirty_count = 0;
        if (!damage_empty) {
            dirty[dirty_count++] = damage_px;
        }
        if (hud_drawn) {
            dirty[dirty_count++] = dip_to_px_rect(hud_box);
        }
        if (dirty_count > 0) {
            DXGI_PRESENT_PARAMETERS params{};
            params.DirtyRectsCount = dirty_count;
            params.pDirtyRects = dirty;
            params.pScrollRect = nullptr;
            params.pScrollOffset = nullptr;
            present_hr = swap_chain_->Present1(1, 0, &params);
        }
        // dirty_count == 0: nothing changed — skip the present entirely;
        // the screen keeps the previously presented frame.
    }
    if (present_hr == DXGI_ERROR_DEVICE_REMOVED ||
        present_hr == DXGI_ERROR_DEVICE_RESET) {
        // Transient GPU reset (TDR etc.): drop all GPU state; the next
        // BeginFrame recreates device, swapchain and targets lazily.
        // Cached bitmaps are device resources and die with the device.
        image_cache_.clear();
        frame_bitmap_.Reset();
        frame_bitmap_size_ = {0, 0};
        resource_cache_.clear();
        video_stages_.clear();
        blur_bitmap_.Reset();
        blur_bitmap_size_ = {0, 0};
        ReleaseTargets();
        swap_chain_.Reset();
        backbuffer_.Reset();
        d3d_device_.Reset();
        dxgi_device_.Reset();
        dxgi_factory_.Reset();
        d2d_device_.Reset();
        dc_.Reset();
        brush_.Reset();
        return;
    }
    if (FAILED(present_hr)) {
        // A driver that rejects Present1 dirty rects (e.g. under remoting)
        // must not kill the renderer: drop to the full-present path for
        // the remaining frames instead of throwing every paint.
        if (!full_present) {
            force_full_present_ = true;
            return;
        }
        ThrowIfFailed(present_hr, "IDXGISwapChain::Present");
    }
}

void Direct2DRenderer::PushClip(Layout::Rect rect) {
    RequireFrame();
    rect = NormalizeRect(rect);
    dc_->PushAxisAlignedClip(
        D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height),
        D2D1_ANTIALIAS_MODE_ALIASED
    );
}

void Direct2DRenderer::PopClip() {
    RequireFrame();
    dc_->PopAxisAlignedClip();
}

void Direct2DRenderer::FillRect(Layout::Rect rect, Theme::Color color, float radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    auto* brush = Brush();
    brush->SetColor(ToColor(color));

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        dc_->FillRectangle(bounds, brush);
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        dc_->FillRoundedRectangle(rounded, brush);
    }
    ++counters_.draw_calls;
}

void Direct2DRenderer::StrokeRect(
    Layout::Rect rect,
    Theme::Color color,
    float width,
    float radius
) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f || !(width > 0.0f)) {
        return;
    }
    auto* brush = Brush();
    brush->SetColor(ToColor(color));

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        dc_->DrawRectangle(bounds, brush, width);
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        dc_->DrawRoundedRectangle(rounded, brush, width);
    }
    ++counters_.draw_calls;
}

void Direct2DRenderer::DrawLine(
    Layout::Point start,
    Layout::Point end,
    Theme::Color color,
    float width
) {
    RequireFrame();
    if (!(width > 0.0f)) {
        return;
    }
    auto* brush = Brush();
    brush->SetColor(ToColor(color));
    dc_->DrawLine(
        D2D1::Point2F(start.x, start.y),
        D2D1::Point2F(end.x, end.y),
        brush,
        width
    );
    ++counters_.draw_calls;
}

void Direct2DRenderer::DrawText(std::string_view text, Layout::Rect rect, TextStyle style) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (text.empty() || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    // TEMP-DIAG entry marker: separates "crashed before DrawTextLayout"
    // from "crashed inside it" in the launch log.
    std::fprintf(stderr, "[DTIN] rect=(%g %g %g %g) size=%g w=%d text=%.40s\n",
                 rect.x, rect.y, rect.width, rect.height, style.size,
                 style.weight, std::string(text).c_str());
    std::fflush(stderr);
    if (text.size() > std::numeric_limits<UINT32>::max()) {
        throw std::length_error("Text exceeds the supported DirectWrite length");
    }

    // Key built into a reused buffer — DrawText runs hundreds of times
    // per frame, and the previous std::to_string key allocated ~6 small
    // strings per call.
    std::string& cache_key = key_scratch_;
    cache_key.clear();
    AppendStyleKey(cache_key, style);
    cache_key.push_back('\x1f');
    AppendNumber(cache_key, rect.width);
    cache_key.push_back('\x1f');
    AppendNumber(cache_key, rect.height);
    cache_key.push_back('\x1f');
    cache_key.append(text);

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (auto* cached = LruGet(text_layouts_, text_layout_lru_, cache_key);
        cached != nullptr) {
        layout = *cached;
    } else {
        const auto wide_text = ToWideString(text);
        const auto format = CreateTextFormat(style);
        ThrowIfFailed(
            write_factory_->CreateTextLayout(
                wide_text.data(),
                static_cast<UINT32>(wide_text.size()),
                format.Get(),
                std::max(rect.width, 0.0f),
                std::max(rect.height, 0.0f),
                layout.GetAddressOf()
            ),
            "IDWriteFactory::CreateTextLayout"
        );
        LruPut(text_layouts_, text_layout_lru_, cache_key, layout, 256);
    }

    auto* brush = Brush();
    brush->SetColor(ToColor(style.color));
    // TEMP-DIAG: isolate the d2d1 AV — log call params before the crash.
    {
        D2D1_MATRIX_3X2_F m{};
        dc_->GetTransform(&m);
        std::fprintf(stderr,
                     "[DTDBG] xform=(%g %g %g %g %g %g) origin=(%g %g) "
                     "rect=(%g %g) size=%g w=%d text=%.40s\n",
                     m._11, m._12, m._21, m._22, m._31, m._32,
                     rect.x, rect.y, rect.width, rect.height,
                     style.size, style.weight,
                     std::string(text).c_str());
        std::fflush(stderr);
    }
    dc_->DrawTextLayout(
        D2D1::Point2F(rect.x, rect.y),
        layout.Get(),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP
    );
    ++counters_.text_calls;
}

Layout::Size Direct2DRenderer::MeasureText(std::string_view text, TextStyle style) const {
    if (text.empty()) {
        return {};
    }
    if (text.size() > std::numeric_limits<UINT32>::max()) {
        throw std::length_error("Text exceeds the supported DirectWrite length");
    }

    std::string& cache_key = key_scratch_;
    cache_key.clear();
    AppendStyleKey(cache_key, style);
    cache_key.push_back('\x1f');
    cache_key.append(text);
    if (auto* cached = LruGet(measure_cache_, measure_lru_, cache_key);
        cached != nullptr) {
        return *cached;
    }

    const auto wide_text = ToWideString(text);
    const auto format = CreateTextFormat(style);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(
        write_factory_->CreateTextLayout(
            wide_text.data(),
            static_cast<UINT32>(wide_text.size()),
            format.Get(),
            1'000'000.0f,
            1'000'000.0f,
            layout.GetAddressOf()
        ),
        "IDWriteFactory::CreateTextLayout"
    );

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    const Layout::Size size(metrics.widthIncludingTrailingWhitespace, metrics.height);
    LruPut(measure_cache_, measure_lru_, cache_key, size, 1024);
    return size;
}

void Direct2DRenderer::RequireFrame() const {
    if (!frame_started_ || dc_ == nullptr) {
        throw std::logic_error("Direct2D drawing requires an active frame");
    }
}

ID2D1SolidColorBrush* Direct2DRenderer::Brush() {
    if (brush_ == nullptr) {
        ThrowIfFailed(
            dc_->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                brush_.GetAddressOf()
            ),
            "ID2D1DeviceContext::CreateSolidColorBrush"
        );
    }
    return brush_.Get();
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> Direct2DRenderer::CreateTextFormat(
    TextStyle style
) const {
    if (!(style.size > 0.0f)) {
        throw std::invalid_argument("Text size must be greater than zero");
    }

    const std::string cache_key = FormatKey(style);
    if (const auto cached = text_formats_.find(cache_key);
        cached != text_formats_.end()) {
        return cached->second;
    }

    // Consolas ships with every Windows since Vista — the guaranteed
    // monospace face (Cascadia is not installed by default).
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    ThrowIfFailed(
        write_factory_->CreateTextFormat(
            style.monospace ? L"Consolas" : L"Segoe UI",
            nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(std::clamp(style.weight, 1, 999)),
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            style.size,
            L"en-us",
            format.GetAddressOf()
        ),
        "IDWriteFactory::CreateTextFormat"
    );
    text_formats_.emplace(cache_key, format);
    return format;
}

D2D1_COLOR_F Direct2DRenderer::ToColor(Theme::Color color) const {
    return D2D1::ColorF(
        std::clamp(color.r, 0.0f, 1.0f),
        std::clamp(color.g, 0.0f, 1.0f),
        std::clamp(color.b, 0.0f, 1.0f),
        std::clamp(color.a, 0.0f, 1.0f)
    );
}

void Direct2DRenderer::DrawBackdropBlur(Layout::Rect rect, float radius) {
    RequireFrame();
    if (!(radius > 0.0f) || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

    // Pure Direct2D elevation/scrim (no GDI GetDC(nullptr) screen capture,
    // which reads the desktop behind DWM-extended Direct2D surfaces).
    RECT client{};
    float client_dip_w = 0.0f;
    float client_dip_h = 0.0f;
    if (hwnd_ != nullptr && GetClientRect(hwnd_, &client)) {
        const float px_to_dip = 96.0f / static_cast<float>(dpi_);
        client_dip_w = static_cast<float>(client.right) * px_to_dip;
        client_dip_h = static_cast<float>(client.bottom) * px_to_dip;
    }

    auto* brush = Brush();
    if (client_dip_w > 0.0f && client_dip_h > 0.0f &&
        rect.width >= client_dip_w * 0.85f &&
        rect.height >= client_dip_h * 0.85f) {
        // Full-window modal backdrop (Dialog).
        brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.48f));
        dc_->FillRectangle(ToRect(rect), brush);
        ++counters_.draw_calls;
        return;
    }

    // Floating popover (ContextMenu / SelectOverlay): multi-step soft drop
    // shadow inside the caller's inflated halo rect.
    constexpr float kAlphas[4] = {0.035f, 0.055f, 0.08f, 0.11f};
    for (int i = 0; i < 4; ++i) {
        const float inset = static_cast<float>(i) * 2.0f;
        const float y_off = static_cast<float>(4 - i) * 1.0f;
        const float w = rect.width - 2.0f * inset;
        const float h = rect.height - 2.0f * inset;
        if (w <= 2.0f || h <= 2.0f) {
            break;
        }
        const D2D1_RECT_F r = D2D1::RectF(
            rect.x + inset,
            rect.y + inset + y_off,
            rect.x + inset + w,
            rect.y + inset + h + y_off);
        const float corner = std::min(12.0f - inset * 0.5f, std::min(w, h) * 0.5f);
        brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, kAlphas[i]));
        dc_->FillRoundedRectangle(
            D2D1::RoundedRect(r, corner, corner), brush);
    }
    ++counters_.draw_calls;
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> Direct2DRenderer::LoadImageBitmap(
    const std::string& path) {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    // WIC needs COM; the app may already be initialized (S_FALSE is fine).
    const HRESULT co_hr =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    (void)co_hr;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic)))) {
        return bitmap;
    }
    const std::wstring wide_path = ToWideString(path);
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(
            wide_path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder))) {
        return bitmap;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return bitmap;
    }
    Microsoft::WRL::ComPtr<IWICBitmapSource> source = frame;
    WICPixelFormatGUID pixel_format{};
    if (SUCCEEDED(frame->GetPixelFormat(&pixel_format)) &&
        pixel_format != GUID_WICPixelFormat32bppPBGRA) {
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(
                frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom))) {
            source = converter;
        }
    }
    dc_->CreateBitmapFromWicBitmap(
        source.Get(), nullptr, bitmap.GetAddressOf());
    return bitmap;
}

bool Direct2DRenderer::DrawImage(std::string_view path, Layout::Rect rect,
                                 float opacity, float corner_radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (path.empty() || rect.width <= 0.0f || rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }

    const std::string key(path);
    auto it = image_cache_.find(key);
    if (it == image_cache_.end()) {
        auto bitmap = LoadImageBitmap(key);
        if (image_cache_.size() >= 64) {
            image_cache_.clear();
        }
        it = image_cache_.emplace(key, std::move(bitmap)).first;
    }
    const auto& bitmap = it->second;
    if (!bitmap) {
        return false;
    }

    const auto bounds = ToRect(rect);
    const D2D1_RECT_F src = D2D1::RectF(
        0.0f, 0.0f,
        static_cast<float>(bitmap->GetPixelSize().width),
        static_cast<float>(bitmap->GetPixelSize().height));
    const float radius = std::clamp(
        corner_radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (radius <= 0.0f) {
        dc_->DrawBitmap(
            bitmap.Get(), &bounds,
            std::clamp(opacity, 0.0f, 1.0f),
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
        ++counters_.image_calls;
        return true;
    }

    // Rounded clip via a geometric-mask layer (works for circles too:
    // radius == half the side yields an ellipse).
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> mask;
    const auto rounded = D2D1::RoundedRect(bounds, radius, radius);
    if (FAILED(d2d_factory_->CreateRoundedRectangleGeometry(
            rounded, mask.GetAddressOf()))) {
        return false;
    }
    const D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
        D2D1::InfiniteRect(), mask.Get(),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.0f, nullptr,
        D2D1_LAYER_OPTIONS_NONE);
    dc_->PushLayer(params, nullptr);
    dc_->DrawBitmap(
        bitmap.Get(), &bounds, std::clamp(opacity, 0.0f, 1.0f),
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    dc_->PopLayer();
    ++counters_.image_calls;
    return true;
}

bool Direct2DRenderer::DrawFrameBgra(const void* bgra, int width, int height,
                                     Layout::Rect dst_rect, float opacity) {
    RequireFrame();
    dst_rect = NormalizeRect(dst_rect);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst_rect.width <= 0.0f || dst_rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }

    // Staging bitmap on the D2D device — recreated only when the frame
    // dimensions change; pixel data is uploaded per frame.
    if (frame_bitmap_ == nullptr ||
        frame_bitmap_size_.width != static_cast<UINT32>(width) ||
        frame_bitmap_size_.height != static_cast<UINT32>(height)) {
        frame_bitmap_.Reset();
        const HRESULT hr = dc_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(width),
                        static_cast<UINT32>(height)),
            nullptr, 0,
            D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                  D2D1_ALPHA_MODE_PREMULTIPLIED),
                static_cast<float>(dpi_), static_cast<float>(dpi_)),
            frame_bitmap_.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        frame_bitmap_size_ = D2D1::SizeU(static_cast<UINT32>(width),
                                         static_cast<UINT32>(height));
    }
    if (FAILED(frame_bitmap_->CopyFromMemory(
            nullptr, bgra, static_cast<UINT32>(width) * 4u))) {
        return false;
    }

    const D2D1_RECT_F dst = ToRect(dst_rect);
    const D2D1_RECT_F src = D2D1::RectF(0.0f, 0.0f,
                                       static_cast<float>(width),
                                       static_cast<float>(height));
    dc_->DrawBitmap(
        frame_bitmap_.Get(), &dst,
        std::clamp(opacity, 0.0f, 1.0f),
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    ++counters_.image_calls;
    return true;
}

void Direct2DRenderer::PushLayer(float opacity,
                                 std::uint32_t blend_mode, float blur) {
    RequireFrame();
    // Blend modes beyond source-over need composition effects; blur
    // inside a layer needs an ID2D1Effect chain — both are accepted
    // and currently fall back to a plain opacity layer.
    (void)blend_mode;
    (void)blur;
    const D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
        D2D1::InfiniteRect(), nullptr,
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(),
        std::clamp(opacity, 0.0f, 1.0f), nullptr,
        D2D1_LAYER_OPTIONS_NONE);
    dc_->PushLayer(params, nullptr);
    ++layer_depth_;
}

void Direct2DRenderer::PopLayer() {
    RequireFrame();
    if (layer_depth_ == 0) {
        return;
    }
    dc_->PopLayer();
    --layer_depth_;
}

void Direct2DRenderer::SetTransform(float a, float b, float c, float d,
                                    float tx, float ty) {
    RequireFrame();
    dc_->SetTransform(D2D1::Matrix3x2F(a, b, c, d, tx, ty));
}

void Direct2DRenderer::ResetTransform() {
    RequireFrame();
    dc_->SetTransform(D2D1::IdentityMatrix());
}

void Direct2DRenderer::DrawBitmapRounded(ID2D1Bitmap* bitmap,
                                       Layout::Rect dst, float opacity,
                                       float corner_radius) {
    const auto bounds = ToRect(dst);
    const D2D1_RECT_F src = D2D1::RectF(
        0.0f, 0.0f,
        static_cast<float>(bitmap->GetPixelSize().width),
        static_cast<float>(bitmap->GetPixelSize().height));
    const float radius = std::clamp(
        corner_radius, 0.0f, std::min(dst.width, dst.height) / 2.0f);
    if (radius <= 0.0f) {
        dc_->DrawBitmap(
            bitmap, &bounds, std::clamp(opacity, 0.0f, 1.0f),
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> mask;
    const auto rounded = D2D1::RoundedRect(bounds, radius, radius);
    if (FAILED(d2d_factory_->CreateRoundedRectangleGeometry(
            rounded, mask.GetAddressOf()))) {
        return;
    }
    const D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
        D2D1::InfiniteRect(), mask.Get(),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.0f, nullptr,
        D2D1_LAYER_OPTIONS_NONE);
    dc_->PushLayer(params, nullptr);
    dc_->DrawBitmap(
        bitmap, &bounds, std::clamp(opacity, 0.0f, 1.0f),
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    dc_->PopLayer();
}

bool Direct2DRenderer::DrawResourcePixels(
    const void* bgra, std::uint32_t id, int width, int height,
    Layout::Rect dst, float opacity, float corner_radius) {
    RequireFrame();
    dst = NormalizeRect(dst);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst.width <= 0.0f || dst.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    auto& bitmap = resource_cache_[id];
    if (bitmap == nullptr) {
        if (resource_cache_.size() >= 256) {
            resource_cache_.clear();  // crude bound; host frees via ids
        }
        const HRESULT hr = dc_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(width),
                        static_cast<UINT32>(height)),
            bgra, static_cast<UINT32>(width) * 4u,
            D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                  D2D1_ALPHA_MODE_PREMULTIPLIED),
                static_cast<float>(dpi_), static_cast<float>(dpi_)),
            bitmap.GetAddressOf());
        if (FAILED(hr) || bitmap == nullptr) {
            return false;
        }
    }
    DrawBitmapRounded(bitmap.Get(), dst, opacity, corner_radius);
    ++counters_.image_calls;
    return true;
}

bool Direct2DRenderer::DrawVideoPixels(
    const void* bgra, std::uint32_t id, int width, int height,
    Layout::Rect dst, float opacity, float corner_radius) {
    RequireFrame();
    dst = NormalizeRect(dst);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst.width <= 0.0f || dst.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    auto& stage = video_stages_[id];
    if (stage.bitmap == nullptr ||
        stage.size.width != static_cast<UINT32>(width) ||
        stage.size.height != static_cast<UINT32>(height)) {
        stage.bitmap.Reset();
        const HRESULT hr = dc_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(width),
                        static_cast<UINT32>(height)),
            nullptr, 0,
            D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                  D2D1_ALPHA_MODE_PREMULTIPLIED),
                static_cast<float>(dpi_), static_cast<float>(dpi_)),
            stage.bitmap.GetAddressOf());
        if (FAILED(hr) || stage.bitmap == nullptr) {
            return false;
        }
        stage.size = D2D1::SizeU(static_cast<UINT32>(width),
                                 static_cast<uint32_t>(height));
    }
    if (FAILED(stage.bitmap->CopyFromMemory(
            nullptr, bgra, static_cast<UINT32>(width) * 4u))) {
        return false;
    }
    DrawBitmapRounded(stage.bitmap.Get(), dst, opacity, corner_radius);
    ++counters_.image_calls;
    return true;
}

void Direct2DRenderer::FreeResource(std::uint32_t id) {
    resource_cache_.erase(id);
    video_stages_.erase(id);
}

Direct2DRenderer::FrameCounters Direct2DRenderer::TakeFrameCounters() {
    const FrameCounters out = counters_;
    counters_ = {};
    return out;
}

void Direct2DRenderer::SetPerfHud(const PerfHudInfo* info) {
    hud_enabled_ = info != nullptr;
    if (info != nullptr) {
        hud_ = *info;
    }
}

bool Direct2DRenderer::DrawPerfHud(Layout::Rect* box_out) {
    if (dc_ == nullptr || box_out == nullptr) {
        return false;
    }
    if (hud_format_ == nullptr) {
        // Consolas keeps the counters column-aligned; created lazily so
        // renderer without HUD pays nothing.
        if (FAILED(write_factory_->CreateTextFormat(
                L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11.0f, L"en-us", hud_format_.GetAddressOf()))) {
            return false;
        }
    }

    wchar_t line[256];
    const int len = std::swprintf(
        line, std::size(line),
        L" %.1fms avg%.1f | dmg %.0f%% | draw %u txt %u img %u | wdg %u/%u",
        hud_.frame_ms, hud_.avg_frame_ms,
        static_cast<double>(hud_.damage_pct), hud_.draw_calls,
        hud_.text_calls, hud_.image_calls, hud_.widgets_painted,
        hud_.widgets_culled);
    if (len <= 0) {
        return false;
    }

    // Bottom-left corner, above the window edge. The box is painted on
    // the swapchain back buffer (never the retained canvas), so it can
    // overlay any content without being repainted by widget damage.
    constexpr float kHudW = 470.0f;
    constexpr float kHudH = 17.0f;
    const float hud_y = std::max(
        0.0f,
        static_cast<float>(target_size_.height) * 96.0f /
                static_cast<float>(dpi_) -
            kHudH - 4.0f);
    const Layout::Rect box{6.0f, hud_y, kHudW, kHudH};
    *box_out = box;

    auto* brush = Brush();
    brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f));
    dc_->FillRectangle(ToRect(box), brush);
    brush->SetColor(D2D1::ColorF(0.35f, 0.95f, 0.45f, 1.0f));
    dc_->DrawText(
        line, static_cast<UINT32>(len), hud_format_.Get(), ToRect(box),
        brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return true;
}

} // namespace XER::UI::Rendering::Windows
