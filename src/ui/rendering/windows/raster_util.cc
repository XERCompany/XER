#include "rendering/windows/raster_util.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>

#include <wincodec.h>
#include <wrl/client.h>

#include "helper/string_utils.h"

#ifdef DrawText
#undef DrawText
#endif

namespace XER::UI::Rendering::Windows {

namespace {

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    return Helper::String::Utf8ToWide(text);
}

} // namespace

bool DecodeImageFileToBgra(std::string_view path,
                           std::vector<std::uint8_t>* out_bgra,
                           int* out_w, int* out_h) {
    if (path.empty() || out_bgra == nullptr || out_w == nullptr ||
        out_h == nullptr) {
        return false;
    }
    // WIC needs COM; the app may already be initialized (S_FALSE is fine).
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic)))) {
        return false;
    }
    const std::wstring wide_path = ToWide(path);
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(
            wide_path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapSource> source = frame;
    WICPixelFormatGUID pixel_format{};
    if (SUCCEEDED(frame->GetPixelFormat(&pixel_format)) &&
        pixel_format != GUID_WICPixelFormat32bppBGRA) {
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(
                frame.Get(), GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom))) {
            source = converter;
        }
    }
    UINT w = 0, h = 0;
    if (FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0) {
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4u);
    if (FAILED(source->CopyPixels(nullptr, w * 4u,
                                  static_cast<UINT>(pixels.size()),
                                  pixels.data()))) {
        return false;
    }
    *out_bgra = std::move(pixels);
    *out_w = static_cast<int>(w);
    *out_h = static_cast<int>(h);
    return true;
}

HFONT GdiText::FontFor(TextStyle style, unsigned dpi) {
    static std::map<std::tuple<int, int, bool>, HFONT> cache;
    const int height_px = std::max(
        1, static_cast<int>(style.size * static_cast<float>(dpi) / 96.0f +
                            0.5f));
    const int weight = std::clamp(style.weight, 1, 999);
    const auto key = std::make_tuple(height_px, weight, style.monospace);
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    HFONT font = CreateFontW(
        -height_px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        style.monospace ? FIXED_PITCH | FF_MODERN
                        : DEFAULT_PITCH | FF_DONTCARE,
        style.monospace ? L"Consolas" : L"Segoe UI");
    cache.emplace(key, font);
    return font;
}

Layout::Size GdiText::Measure(std::string_view text, TextStyle style,
                              unsigned dpi, float wrap_width_px) {
    if (text.empty()) {
        return {};
    }
    const std::wstring wide = ToWide(text);
    if (wide.empty()) {
        return {};
    }
    HFONT font = FontFor(style, dpi);
    if (font == nullptr) {
        return {};
    }
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) {
        return {};
    }
    HDC mem = CreateCompatibleDC(dc);
    ReleaseDC(nullptr, dc);
    if (mem == nullptr) {
        return {};
    }
    HGDIOBJ old = SelectObject(mem, font);
    RECT rc{0, 0,
            wrap_width_px > 0.0f ? static_cast<LONG>(wrap_width_px) : 1000000,
            1000000};
    const int height = DrawTextW(
        mem, wide.data(), static_cast<int>(wide.size()), &rc,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_LEFT | DT_TOP);
    SelectObject(mem, old);
    DeleteDC(mem);
    const float dip = 96.0f / static_cast<float>(dpi);
    return {static_cast<float>(rc.right) * dip,
            static_cast<float>(height > 0 ? rc.bottom : 0) * dip};
}

bool GdiText::RasterizeMask(std::string_view text, TextStyle style,
                            int width_px, int height_px,
                            std::vector<std::uint8_t>* out_bgra) {
    if (text.empty() || width_px <= 0 || height_px <= 0 ||
        out_bgra == nullptr) {
        return false;
    }
    const std::wstring wide = ToWide(text);
    if (wide.empty()) {
        return false;
    }
    // style.size is already in device pixels (contract on the header);
    // dpi=96 keeps FontFor's DIP->px mapping a no-op.
    HFONT font = FontFor(style, 96);
    if (font == nullptr) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_px;
    bmi.bmiHeader.biHeight = -height_px;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
    if (screen != nullptr) {
        ReleaseDC(nullptr, screen);
    }
    if (dib == nullptr || bits == nullptr) {
        return false;
    }
    // White background: coverage = 1 - luminance yields 0 alpha there.
    std::memset(bits, 0xFF, static_cast<std::size_t>(width_px) * height_px * 4u);

    HDC mem = CreateCompatibleDC(nullptr);
    if (mem == nullptr) {
        DeleteObject(dib);
        return false;
    }
    HGDIOBJ old_bmp = SelectObject(mem, dib);
    HGDIOBJ old_font = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0, 0, 0));
    RECT rc{0, 0, width_px, height_px};
    DrawTextW(mem, wide.data(), static_cast<int>(wide.size()), &rc,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    GdiFlush();
    SelectObject(mem, old_font);
    SelectObject(mem, old_bmp);
    DeleteDC(mem);

    out_bgra->assign(static_cast<const std::uint8_t*>(bits),
                     static_cast<const std::uint8_t*>(bits) +
                         static_cast<std::size_t>(width_px) * height_px * 4u);
    DeleteObject(dib);
    return true;
}

} // namespace XER::UI::Rendering::Windows
