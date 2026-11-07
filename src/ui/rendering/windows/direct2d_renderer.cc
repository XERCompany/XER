#include "direct2d_renderer.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
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

void ValidateRect(Layout::Rect rect) {
    if (rect.width < 0.0f || rect.height < 0.0f) {
        throw std::invalid_argument("Drawing rectangles cannot have negative dimensions");
    }
}

std::wstring ToWideString(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Text exceeds the supported UTF-8 length");
    }

    std::wstring wide = XER::Engine::Helper::String::Utf8ToWide(text);
    if (wide.empty() && !text.empty()) {
        throw std::runtime_error("Text is not valid UTF-8");
    }
    return wide;
}

std::string FormatKey(const TextStyle& style) {
    return std::to_string(style.size) + "|" + std::to_string(style.weight);
}

// One separable box-blur pass over interleaved BGRA pixels using a sliding
// window. `horizontal` selects the pass direction; edges clamp.
void BoxBlurPass(std::vector<std::uint8_t>& px, int w, int h, int radius,
                 bool horizontal) {
    if (radius <= 0 || w <= 0 || h <= 0) return;
    const int n = horizontal ? w : h;
    const int m = horizontal ? h : w;
    const int r = std::min(radius, std::max(0, n / 2));
    if (r <= 0) return;
    const int win = 2 * r + 1;

    auto at = [&](int i, int line) -> const std::uint8_t* {
        const int x = horizontal ? i : line;
        const int y = horizontal ? line : i;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    };

    std::vector<std::uint8_t> tmp(px.size());
    for (int line = 0; line < m; ++line) {
        std::uint64_t sum[4] = {0, 0, 0, 0};
        for (int i = -r; i <= r; ++i) {
            const std::uint8_t* p = at(std::clamp(i, 0, n - 1), line);
            for (int c = 0; c < 4; ++c) sum[c] += p[c];
        }
        for (int i = 0; i < n; ++i) {
            std::uint8_t* d = horizontal
                ? &tmp[(static_cast<std::size_t>(line) * w + i) * 4]
                : &tmp[(static_cast<std::size_t>(i) * w + line) * 4];
            for (int c = 0; c < 4; ++c) {
                d[c] = static_cast<std::uint8_t>(sum[c] / win);
            }
            const std::uint8_t* out_p = at(std::clamp(i - r, 0, n - 1), line);
            const std::uint8_t* in_p = at(std::clamp(i + r + 1, 0, n - 1), line);
            for (int c = 0; c < 4; ++c) {
                sum[c] += in_p[c] - out_p[c];
            }
        }
    }
    px.swap(tmp);
}

// Box-blur iterations ≈ a gaussian blur; fewer passes = subtler frost.
void BoxBlurN(std::vector<std::uint8_t>& px, int w, int h, int radius,
              int iterations) {
    for (int i = 0; i < iterations; ++i) {
        BoxBlurPass(px, w, h, radius, true);
        BoxBlurPass(px, w, h, radius, false);
    }
}

} // namespace

Direct2DRenderer::Direct2DRenderer(HWND hwnd)
    : hwnd_(hwnd)
    , dpi_(hwnd != nullptr ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI) {
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }

    ThrowIfFailed(
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory_.GetAddressOf()),
        "D2D1CreateFactory"
    );
    ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())
        ),
        "DWriteCreateFactory"
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
    if (render_target_ != nullptr) {
        render_target_.Reset();
        brush_.Reset();
        target_size_ = {0, 0};
    }
}

void Direct2DRenderer::BeginFrame(Layout::Rect bounds) {
    if (frame_started_) {
        throw std::logic_error("A Direct2D frame is already active");
    }
    if (bounds.width < 0.0f || bounds.height < 0.0f) {
        throw std::invalid_argument("Frame bounds cannot have negative dimensions");
    }

    EnsureRenderTarget();
    RECT client_rect{};
    if (!GetClientRect(hwnd_, &client_rect)) {
        throw std::runtime_error("GetClientRect failed while beginning a Direct2D frame");
    }

    const auto pixel_width = static_cast<UINT32>(std::max<LONG>(1, client_rect.right));
    const auto pixel_height = static_cast<UINT32>(std::max<LONG>(1, client_rect.bottom));
    const D2D1_SIZE_U target_size = D2D1::SizeU(pixel_width, pixel_height);
    if (target_size.width != target_size_.width ||
        target_size.height != target_size_.height) {
        const auto resize_result = render_target_->Resize(target_size);
        if (FAILED(resize_result)) {
            if (resize_result == D2DERR_RECREATE_TARGET) {
                render_target_.Reset();
                brush_.Reset();
            }
            ThrowIfFailed(resize_result, "ID2D1HwndRenderTarget::Resize");
        }
        target_size_ = target_size;
    }

    render_target_->BeginDraw();
    render_target_->Clear(ToColor(clear_color_));
    frame_started_ = true;
}

void Direct2DRenderer::EndFrame() {
    RequireFrame();
    frame_started_ = false;
    const auto result = render_target_->EndDraw();
    if (FAILED(result)) {
        if (result == D2DERR_RECREATE_TARGET) {
            render_target_.Reset();
            brush_.Reset();
        }
        ThrowIfFailed(result, "ID2D1HwndRenderTarget::EndDraw");
    }
}

void Direct2DRenderer::PushClip(Layout::Rect rect) {
    RequireFrame();
    render_target_->PushAxisAlignedClip(
        D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height),
        D2D1_ANTIALIAS_MODE_ALIASED
    );
}

void Direct2DRenderer::PopClip() {
    RequireFrame();
    render_target_->PopAxisAlignedClip();
}

void Direct2DRenderer::FillRect(Layout::Rect rect, Theme::Color color, float radius) {
    RequireFrame();
    ValidateRect(rect);
    auto* brush = Brush();
    brush->SetColor(ToColor(color));

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        render_target_->FillRectangle(bounds, brush);
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        render_target_->FillRoundedRectangle(rounded, brush);
    }
}

void Direct2DRenderer::StrokeRect(
    Layout::Rect rect,
    Theme::Color color,
    float width,
    float radius
) {
    RequireFrame();
    ValidateRect(rect);
    if (!(width > 0.0f)) {
        throw std::invalid_argument("Stroke width must be greater than zero");
    }
    auto* brush = Brush();
    brush->SetColor(ToColor(color));

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        render_target_->DrawRectangle(bounds, brush, width);
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        render_target_->DrawRoundedRectangle(rounded, brush, width);
    }
}

void Direct2DRenderer::DrawLine(
    Layout::Point start,
    Layout::Point end,
    Theme::Color color,
    float width
) {
    RequireFrame();
    if (!(width > 0.0f)) {
        throw std::invalid_argument("Line width must be greater than zero");
    }
    auto* brush = Brush();
    brush->SetColor(ToColor(color));
    render_target_->DrawLine(
        D2D1::Point2F(start.x, start.y),
        D2D1::Point2F(end.x, end.y),
        brush,
        width
    );
}

void Direct2DRenderer::DrawText(std::string_view text, Layout::Rect rect, TextStyle style) {
    RequireFrame();
    ValidateRect(rect);
    if (text.empty()) {
        return;
    }
    if (text.size() > std::numeric_limits<UINT32>::max()) {
        throw std::length_error("Text exceeds the supported DirectWrite length");
    }

    const std::string cache_key = FormatKey(style) + "\x1f" +
        std::to_string(rect.width) + "\x1f" +
        std::to_string(rect.height) + "\x1f" + std::string(text);

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (const auto cached = text_layouts_.find(cache_key);
        cached != text_layouts_.end()) {
        layout = cached->second;
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
        if (text_layouts_.size() >= 256) {
            text_layouts_.clear();
        }
        text_layouts_.emplace(cache_key, layout);
    }

    auto* brush = Brush();
    brush->SetColor(ToColor(style.color));
    render_target_->DrawTextLayout(
        D2D1::Point2F(rect.x, rect.y),
        layout.Get(),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP
    );
}

Layout::Size Direct2DRenderer::MeasureText(std::string_view text, TextStyle style) const {
    if (text.empty()) {
        return {};
    }
    if (text.size() > std::numeric_limits<UINT32>::max()) {
        throw std::length_error("Text exceeds the supported DirectWrite length");
    }

    const std::string cache_key = FormatKey(style) + "\x1f" + std::string(text);
    if (const auto cached = measure_cache_.find(cache_key);
        cached != measure_cache_.end()) {
        return cached->second;
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
    if (measure_cache_.size() >= 1024) {
        measure_cache_.clear();
    }
    measure_cache_.emplace(cache_key, size);
    return size;
}

void Direct2DRenderer::EnsureRenderTarget() {
    if (render_target_) {
        return;
    }
    if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
        throw std::runtime_error("Cannot create a Direct2D render target for an invalid HWND");
    }

    RECT client_rect{};
    if (!GetClientRect(hwnd_, &client_rect)) {
        throw std::runtime_error("GetClientRect failed while creating a Direct2D render target");
    }
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(1, client_rect.right)),
        static_cast<UINT32>(std::max<LONG>(1, client_rect.bottom))
    );
    ThrowIfFailed(
        d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, size),
            render_target_.GetAddressOf()
        ),
        "ID2D1Factory::CreateHwndRenderTarget"
    );
    render_target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    target_size_ = size;
}

void Direct2DRenderer::RequireFrame() const {
    if (!frame_started_ || render_target_ == nullptr) {
        throw std::logic_error("Direct2D drawing requires an active frame");
    }
}

ID2D1SolidColorBrush* Direct2DRenderer::Brush() {
    if (brush_ == nullptr) {
        ThrowIfFailed(
            render_target_->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                brush_.GetAddressOf()
            ),
            "ID2D1RenderTarget::CreateSolidColorBrush"
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

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    ThrowIfFailed(
        write_factory_->CreateTextFormat(
            L"Segoe UI",
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
    if (!(radius > 0.5f) || hwnd_ == nullptr) {
        return;
    }

    // Widget coordinates are DIPs; GDI works in physical pixels.
    const float dip_to_px = dpi_ / 96.0f;
    RECT client{};
    if (!GetClientRect(hwnd_, &client) || client.right <= 0 ||
        client.bottom <= 0) {
        return;
    }

    float x0 = rect.x * dip_to_px;
    float y0 = rect.y * dip_to_px;
    float x1 = (rect.x + rect.width) * dip_to_px;
    float y1 = (rect.y + rect.height) * dip_to_px;
    x0 = std::clamp(x0, 0.0f, static_cast<float>(client.right));
    y0 = std::clamp(y0, 0.0f, static_cast<float>(client.bottom));
    x1 = std::clamp(x1, 0.0f, static_cast<float>(client.right));
    y1 = std::clamp(y1, 0.0f, static_cast<float>(client.bottom));
    if (x1 - x0 < 4.0f || y1 - y0 < 4.0f) {
        return;
    }

    // Capture a region inflated by the blur bleed, in screen coordinates.
    const int margin = static_cast<int>(std::ceil(radius * dip_to_px * 1.5f));
    POINT origin{0, 0};
    if (!ClientToScreen(hwnd_, &origin)) {
        return;
    }
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    int cap_x = origin.x + static_cast<int>(std::floor(x0)) - margin;
    int cap_y = origin.y + static_cast<int>(std::floor(y0)) - margin;
    int cap_w = static_cast<int>(std::ceil(x1 - x0)) + 2 * margin;
    int cap_h = static_cast<int>(std::ceil(y1 - y0)) + 2 * margin;
    cap_x = std::clamp(cap_x, vx, std::max(vx, vx + vw - cap_w));
    cap_y = std::clamp(cap_y, vy, std::max(vy, vy + vh - cap_h));
    cap_w = std::min(cap_w, vw);
    cap_h = std::min(cap_h, vh);
    if (cap_w < 8 || cap_h < 8) {
        return;
    }

    // BitBlt the screen region into a top-down 32bpp DIB.
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) return;
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cap_w;
    bmi.bmiHeader.biHeight = -cap_h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* dib_bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &dib_bits,
                                   nullptr, 0);
    std::vector<std::uint8_t> work;
    bool captured = false;
    if (dib != nullptr && dib_bits != nullptr && mem_dc != nullptr) {
        HBITMAP old_bmp =
            static_cast<HBITMAP>(SelectObject(mem_dc, dib));
        if (BitBlt(mem_dc, 0, 0, cap_w, cap_h, screen_dc, cap_x, cap_y,
                   SRCCOPY) != 0) {
            work.assign(static_cast<std::size_t>(cap_w) * cap_h * 4, 0);
            const auto* src = static_cast<const std::uint8_t*>(dib_bits);
            // DIB rows are already top-down (negative height); force alpha.
            for (int y = 0; y < cap_h; ++y) {
                std::memcpy(work.data() + static_cast<std::size_t>(y) * cap_w * 4,
                            src + static_cast<std::size_t>(y) * cap_w * 4,
                            static_cast<std::size_t>(cap_w) * 4);
                std::uint8_t* row = work.data() +
                    static_cast<std::size_t>(y) * cap_w * 4;
                for (int x = 0; x < cap_w; ++x) {
                    row[x * 4 + 3] = 255;
                }
            }
            captured = true;
        }
        SelectObject(mem_dc, old_bmp);
    }
    if (dib != nullptr) DeleteObject(dib);
    if (mem_dc != nullptr) DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    if (!captured) {
        return;
    }

    // Downsample: average cap_w×cap_h blocks into dw×dh, then box-blur.
    // Working at reduced resolution keeps the CPU cost trivial even for
    // fullscreen blur.
    const float radius_px = radius * dip_to_px;
    // Downsample factor grows with radius but may be 1 — forcing f>=2 made
    // even tiny radii look heavily blurred (2x box downsample alone reads
    // as strong blur). Full-res + small kernel = subtle frost.
    const int f = std::clamp(
        static_cast<int>(std::lround(radius_px / 10.0f)), 1, 8);
    const int dw = std::max(1, cap_w / f);
    const int dh = std::max(1, cap_h / f);
    std::vector<std::uint8_t> ds_px(static_cast<std::size_t>(dw) * dh * 4, 0);
    for (int dy = 0; dy < dh; ++dy) {
        const int y_begin = dy * f;
        const int y_end = std::min(cap_h, y_begin + f);
        for (int dx = 0; dx < dw; ++dx) {
            const int x_begin = dx * f;
            const int x_end = std::min(cap_w, x_begin + f);
            std::uint64_t sum[4] = {0, 0, 0, 0};
            std::uint64_t count = 0;
            for (int y = y_begin; y < y_end; ++y) {
                const std::uint8_t* row =
                    work.data() + static_cast<std::size_t>(y) * cap_w * 4;
                for (int x = x_begin; x < x_end; ++x) {
                    const std::uint8_t* p = row + x * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                    ++count;
                }
            }
            std::uint8_t* d =
                ds_px.data() + (static_cast<std::size_t>(dy) * dw + dx) * 4;
            for (int c = 0; c < 4; ++c) {
                d[c] = static_cast<std::uint8_t>(sum[c] / count);
            }
        }
    }
    const int r_ds = std::clamp(
        static_cast<int>(std::lround(radius_px / (f * 2.0f))), 1, 32);
    // Tiny radii get a single blur iteration — a whisper of frost, not a
    // smear (and ~3x cheaper per frame).
    BoxBlurN(ds_px, dw, dh, r_ds, radius_px < 4.0f ? 1 : 3);

    // Upload (cached bitmap, recreated only on size change).
    if (blur_bitmap_ == nullptr || blur_bitmap_size_.width !=
            static_cast<UINT32>(dw) ||
        blur_bitmap_size_.height != static_cast<UINT32>(dh)) {
        blur_bitmap_.Reset();
        ThrowIfFailed(
            render_target_->CreateBitmap(
                D2D1::SizeU(static_cast<UINT32>(dw),
                            static_cast<UINT32>(dh)),
                nullptr, 0,
                D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED),
                    static_cast<float>(dpi_), static_cast<float>(dpi_)),
                blur_bitmap_.GetAddressOf()),
            "ID2D1RenderTarget::CreateBitmap (backdrop blur)");
        blur_bitmap_size_ = D2D1::SizeU(static_cast<UINT32>(dw),
                                        static_cast<UINT32>(dh));
    }
    ThrowIfFailed(
        blur_bitmap_->CopyFromMemory(nullptr, ds_px.data(),
                                     static_cast<UINT32>(dw) * 4),
        "ID2D1Bitmap::CopyFromMemory (backdrop blur)");

    // Paint the blurred capture back over the same client-space rect,
    // inflated by the blur margin so the frost bleeds past the panel edge.
    const float left = (cap_x - origin.x) / dip_to_px;
    const float top = (cap_y - origin.y) / dip_to_px;
    const float w = cap_w / dip_to_px;
    const float h = cap_h / dip_to_px;
    const D2D1_RECT_F dest = D2D1::RectF(left, top, left + w, top + h);
    render_target_->DrawBitmap(
        blur_bitmap_.Get(), &dest, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
    render_target_->CreateBitmapFromWicBitmap(
        source.Get(), nullptr, bitmap.GetAddressOf());
    return bitmap;
}

bool Direct2DRenderer::DrawImage(std::string_view path, Layout::Rect rect,
                                 float opacity, float corner_radius) {
    RequireFrame();
    ValidateRect(rect);
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
        render_target_->DrawBitmap(
            bitmap.Get(), &bounds,
            std::clamp(opacity, 0.0f, 1.0f),
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
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
    render_target_->PushLayer(params, nullptr);
    render_target_->DrawBitmap(
        bitmap.Get(), &bounds, std::clamp(opacity, 0.0f, 1.0f),
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    render_target_->PopLayer();
    return true;
}

bool Direct2DRenderer::DrawFrameBgra(const void* bgra, int width, int height,
                                     Layout::Rect dst_rect, float opacity) {
    RequireFrame();
    ValidateRect(dst_rect);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst_rect.width <= 0.0f || dst_rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }

    // Staging bitmap on the D2D (D3D-backed) target — recreated only when
    // the frame dimensions change; pixel data is uploaded per frame.
    if (frame_bitmap_ == nullptr ||
        frame_bitmap_size_.width != static_cast<UINT32>(width) ||
        frame_bitmap_size_.height != static_cast<UINT32>(height)) {
        frame_bitmap_.Reset();
        const HRESULT hr = render_target_->CreateBitmap(
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
    render_target_->DrawBitmap(
        frame_bitmap_.Get(), &dst, std::clamp(opacity, 0.0f, 1.0f),
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    return true;
}

} // namespace XER::UI::Rendering::Windows
