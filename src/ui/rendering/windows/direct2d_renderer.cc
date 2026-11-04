#include "direct2d_renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include <windows.h>

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

    const auto length = static_cast<int>(text.size());
    const int wide_length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        length,
        nullptr,
        0
    );
    if (wide_length == 0) {
        throw std::runtime_error("Text is not valid UTF-8");
    }

    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            length,
            wide.data(),
            wide_length
        ) != wide_length) {
        throw std::runtime_error("UTF-8 text conversion failed");
    }
    return wide;
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
    const auto resize_result = render_target_->Resize(target_size);
    if (FAILED(resize_result)) {
        if (resize_result == D2DERR_RECREATE_TARGET) {
            render_target_.Reset();
        }
        ThrowIfFailed(resize_result, "ID2D1HwndRenderTarget::Resize");
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
        }
        ThrowIfFailed(result, "ID2D1HwndRenderTarget::EndDraw");
    }
}

void Direct2DRenderer::FillRect(Layout::Rect rect, Theme::Color color, float radius) {
    RequireFrame();
    ValidateRect(rect);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    ThrowIfFailed(
        render_target_->CreateSolidColorBrush(ToColor(color), brush.GetAddressOf()),
        "ID2D1RenderTarget::CreateSolidColorBrush"
    );

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        render_target_->FillRectangle(bounds, brush.Get());
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        render_target_->FillRoundedRectangle(rounded, brush.Get());
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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    ThrowIfFailed(
        render_target_->CreateSolidColorBrush(ToColor(color), brush.GetAddressOf()),
        "ID2D1RenderTarget::CreateSolidColorBrush"
    );

    const auto bounds = ToRect(rect);
    const float safe_radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) / 2.0f);
    if (safe_radius == 0.0f) {
        render_target_->DrawRectangle(bounds, brush.Get(), width);
    } else {
        const auto rounded = D2D1::RoundedRect(bounds, safe_radius, safe_radius);
        render_target_->DrawRoundedRectangle(rounded, brush.Get(), width);
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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    ThrowIfFailed(
        render_target_->CreateSolidColorBrush(ToColor(color), brush.GetAddressOf()),
        "ID2D1RenderTarget::CreateSolidColorBrush"
    );
    render_target_->DrawLine(
        D2D1::Point2F(start.x, start.y),
        D2D1::Point2F(end.x, end.y),
        brush.Get(),
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

    const auto wide_text = ToWideString(text);
    const auto format = CreateTextFormat(style);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
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

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    ThrowIfFailed(
        render_target_->CreateSolidColorBrush(ToColor(style.color), brush.GetAddressOf()),
        "ID2D1RenderTarget::CreateSolidColorBrush"
    );
    render_target_->DrawTextLayout(
        D2D1::Point2F(rect.x, rect.y),
        layout.Get(),
        brush.Get(),
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
    return Layout::Size(metrics.widthIncludingTrailingWhitespace, metrics.height);
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
}

void Direct2DRenderer::RequireFrame() const {
    if (!frame_started_ || render_target_ == nullptr) {
        throw std::logic_error("Direct2D drawing requires an active frame");
    }
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> Direct2DRenderer::CreateTextFormat(
    TextStyle style
) const {
    if (!(style.size > 0.0f)) {
        throw std::invalid_argument("Text size must be greater than zero");
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    ThrowIfFailed(
        write_factory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            style.size,
            L"en-us",
            format.GetAddressOf()
        ),
        "IDWriteFactory::CreateTextFormat"
    );
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

} // namespace XER::UI::Rendering::Windows
