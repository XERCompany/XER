#include "toast.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// WIN32_LEAN_AND_MEAN strips objidl.h, which GdiplusBitmap.h needs for
// IStream — pull it explicitly before gdiplus.h.
#include <objidl.h>

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace XER::UI::Overlays {
namespace {

constexpr wchar_t kClassName[] = L"XERToastOverlay";
constexpr UINT kMsgShow = WM_APP + 1;
constexpr UINT kMsgClear = WM_APP + 2;
constexpr UINT_PTR kTimerFrame = 1;

struct ColorRGBA {
    BYTE r, g, b, a;
};

ColorRGBA TypeAccent(ToastType type) {
    switch (type) {
        case ToastType::Success: return {0x3F, 0xB9, 0x50, 0xFF};
        case ToastType::Warning: return {0xD2, 0x99, 0x22, 0xFF};
        case ToastType::Error:   return {0xF8, 0x51, 0x49, 0xFF};
        case ToastType::Info:
        default:                 return {0x4C, 0x8D, 0xFF, 0xFF};
    }
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr,
                                      0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

ToastManager::Impl*& ActiveImpl();
LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

} // namespace

struct ToastManager::Impl {
    struct Toast {
        ToastOptions opt;
        HWND hwnd = nullptr;
        int w = 0;
        int h = 0;
        float x = 0.0f, y = 0.0f;
        float tx = 0.0f, ty = 0.0f;
        float alpha = 0.0f;
        bool closing = false;
        DWORD born = 0;
        float anim_t = 0.0f;
        RECT close_rect{};

        HDC memdc = nullptr;
        HBITMAP dib = nullptr;
        void* bits = nullptr;
        // Non-owning wrapper over the DIB bits — destroyed before dib.
        std::unique_ptr<Gdiplus::Bitmap> bitmap;
        std::unique_ptr<Gdiplus::Graphics> gfx;
        std::unique_ptr<Gdiplus::Font> font_title;
        std::unique_ptr<Gdiplus::Font> font_body;
        std::unique_ptr<Gdiplus::FontFamily> family;
        std::unique_ptr<Gdiplus::StringFormat> format;
        std::unique_ptr<Gdiplus::Image> icon;

        ~Toast() {
            if (dib) DeleteObject(dib);
            if (memdc) DeleteDC(memdc);
        }
    };

    ~Impl() { Shutdown(); }

    void Shutdown() {
        if (thread_.joinable()) {
            PostThreadMessage(thread_id_, WM_QUIT, 0, 0);
            thread_.join();
        }
    }

    void Start() {
        std::call_once(start_once_, [this] {
            ready_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            thread_ = std::thread([this] { ThreadMain(); });
            WaitForSingleObject(ready_, 5000);
            CloseHandle(ready_);
            ready_ = nullptr;
        });
    }

    void Show(ToastOptions options) {
        Start();
        auto* data = new ToastOptions(std::move(options));
        PostThreadMessage(thread_id_, kMsgShow,
                          reinterpret_cast<WPARAM>(data), 0);
    }

    void Clear() {
        if (thread_id_ != 0)
            PostThreadMessage(thread_id_, kMsgClear, 0, 0);
    }

    void ThreadMain() {
        ActiveImpl() = this;

        ULONG_PTR gdip_token = 0;
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&gdip_token, &si, nullptr);

        WNDCLASSW wc{};
        wc.lpfnWndProc = ToastWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
        wc.style = CS_OWNDC;
        RegisterClassW(&wc);

        thread_id_ = GetCurrentThreadId();
        SetEvent(ready_);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            if (msg.message == kMsgShow) {
                auto* data = reinterpret_cast<ToastOptions*>(msg.wParam);
                HandleShow(*data);
                delete data;
                continue;
            }
            if (msg.message == kMsgClear) {
                for (auto* t : toasts_) RequestClose(t);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        for (auto* t : toasts_) {
            if (t->hwnd) DestroyWindow(t->hwnd);
            delete t;
        }
        toasts_.clear();

        Gdiplus::GdiplusShutdown(gdip_token);
    }

    void HandleShow(const ToastOptions& options) {
        auto* t = new Toast();
        t->opt = options;
        t->born = GetTickCount();

        const float scale = GetDpiForSystem() / 96.0f;
        EnsureFonts(t, scale);
        const int w = static_cast<int>(kCardWidthDip * scale);
        const int h = MeasureHeight(t, w, scale);
        t->w = w;
        t->h = h;

        const RECT work = WorkArea();
        t->tx = static_cast<float>(work.right + w);
        t->ty = static_cast<float>(work.bottom - kMargin - h);
        t->x = t->tx;
        t->y = t->ty;
        t->alpha = 0.0f;
        t->anim_t = 0.0f;

        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kClassName, L"", WS_POPUP, static_cast<int>(t->x),
            static_cast<int>(t->y), w, h, nullptr, nullptr,
            GetModuleHandleW(nullptr), t);
        if (!hwnd) {
            delete t;
            return;
        }
        t->hwnd = hwnd;
        SetupSurface(t);
        SetTimer(hwnd, kTimerFrame, 16, nullptr);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        toasts_.push_back(t);
        Relayout();
        RenderFrame(t);
    }

    void Relayout() {
        if (toasts_.empty()) return;
        const RECT work = WorkArea();
        float y = static_cast<float>(work.bottom - kMargin - toasts_.back()->h);
        for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
            Toast* t = *it;
            if (t->closing) continue;
            t->tx = static_cast<float>(work.right - kMargin - t->w);
            t->ty = y;
            y -= static_cast<float>(t->h + kGap);
        }
    }

    RECT WorkArea() const {
        RECT rc{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
        return rc;
    }

    void RequestClose(Toast* t) {
        if (!t || t->closing) return;
        t->closing = true;
        const RECT work = WorkArea();
        t->tx = static_cast<float>(work.right + t->w + kMargin);
    }

    void RemoveToast(Toast* t) {
        auto it = std::find(toasts_.begin(), toasts_.end(), t);
        if (it != toasts_.end()) toasts_.erase(it);
        Relayout();
    }

    void EnsureFonts(Toast* t, float scale) {
        if (t->family) return;
        t->family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
        if (t->family->GetLastStatus() != Gdiplus::Ok) {
            t->family = std::make_unique<Gdiplus::FontFamily>(L"Tahoma");
        }
        t->font_title = std::make_unique<Gdiplus::Font>(
            t->family.get(), 13.0f * scale, Gdiplus::FontStyleBold,
            Gdiplus::UnitPixel);
        t->font_body = std::make_unique<Gdiplus::Font>(
            t->family.get(), 12.0f * scale, Gdiplus::FontStyleRegular,
            Gdiplus::UnitPixel);
        t->format = std::make_unique<Gdiplus::StringFormat>(
            Gdiplus::StringFormat::GenericDefault());
        t->format->SetLineAlignment(Gdiplus::StringAlignmentNear);
    }

    int MeasureHeight(Toast* t, int w, float scale) {
        Gdiplus::Bitmap scratch(4, 4, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(&scratch);
        const float pad = 14.0f * scale;
        const float close_w = 28.0f * scale;
        const float text_w =
            static_cast<float>(w) - pad * 2.0f - close_w - 8.0f * scale;

        float h = pad * 2.0f;
        const std::wstring title = Utf8ToWide(t->opt.title);
        const std::wstring msg = Utf8ToWide(t->opt.message);
        const Gdiplus::RectF area(0, 0, text_w, 10000.0f);
        Gdiplus::RectF measured{};
        if (!title.empty()) {
            g.MeasureString(title.c_str(), -1, t->font_title.get(), area,
                            t->format.get(), &measured);
            h += measured.Height;
        }
        if (!msg.empty()) {
            const float gap = title.empty() ? 0.0f : 4.0f * scale;
            g.MeasureString(msg.c_str(), -1, t->font_body.get(), area,
                            t->format.get(), &measured);
            h += gap + measured.Height;
        }
        if (!t->opt.icon_path.empty()) {
            h = std::max(h, pad * 2.0f + 36.0f * scale);
        }
        const float min_h =
            (title.empty() && msg.empty()) ? 56.0f * scale
                                           : pad * 2.0f + 16.0f * scale;
        const float max_h = pad * 2.0f + 96.0f * scale;
        return static_cast<int>(std::clamp(h, min_h, max_h));
    }

    void SetupSurface(Toast* t) {
        // 32bpp top-down DIB + memory DC; the GDI+ Bitmap wraps the raw
        // bits (non-owning) so drawing lands directly in the blit source.
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = t->w;
        bmi.bmiHeader.biHeight = -t->h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        t->memdc = CreateCompatibleDC(nullptr);
        t->dib = CreateDIBSection(t->memdc, &bmi, DIB_RGB_COLORS,
                                  &t->bits, nullptr, 0);
        SelectObject(t->memdc, t->dib);
        t->bitmap = std::make_unique<Gdiplus::Bitmap>(
            t->w, t->h, t->w * 4, PixelFormat32bppPARGB,
            static_cast<BYTE*>(t->bits));
        t->gfx = std::make_unique<Gdiplus::Graphics>(t->bitmap.get());
        t->gfx->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        t->gfx->SetTextRenderingHint(
            Gdiplus::TextRenderingHintClearTypeGridFit);
        if (!t->opt.icon_path.empty()) {
            const std::wstring wpath = Utf8ToWide(t->opt.icon_path);
            t->icon.reset(Gdiplus::Image::FromFile(wpath.c_str()));
            if (t->icon && t->icon->GetLastStatus() != Gdiplus::Ok) {
                t->icon.reset();
            }
        }
    }

    void RenderFrame(Toast* t) {
        const float scale = GetDpiForSystem() / 96.0f;
        const float alpha = t->alpha / 255.0f;
        auto* g = t->gfx.get();
        g->Clear(Gdiplus::Color(0, 0, 0, 0));

        const Gdiplus::RectF card(0.5f, 0.5f,
                                  static_cast<float>(t->w) - 1.0f,
                                  static_cast<float>(t->h) - 1.0f);
        const float radius = 10.0f * scale;
        const float d = radius * 2.0f;

        {
            Gdiplus::GraphicsPath path;
            path.AddArc(card.X, card.Y, d, d, 180, 90);
            path.AddArc(card.X + card.Width - d, card.Y, d, d, 270, 90);
            path.AddArc(card.X + card.Width - d, card.Y + card.Height - d, d, d,
                        0, 90);
            path.AddArc(card.X, card.Y + card.Height - d, d, d, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush bg(
                Gdiplus::Color(static_cast<BYTE>(240.0f * alpha), 32, 34,
                               40));
            g->FillPath(&bg, &path);
        }

        // Type accent bar.
        {
            const auto ac = TypeAccent(t->opt.type);
            Gdiplus::SolidBrush accent(Gdiplus::Color(
                static_cast<BYTE>(255 * alpha), ac.r, ac.g, ac.b));
            const float bar_w = 4.0f * scale;
            const float bx = card.X + 7.0f * scale;
            const float by = card.Y + radius * 0.7f;
            const float bh = card.Height - radius * 1.4f;
            const float br = bar_w * 0.5f;
            Gdiplus::GraphicsPath bar;
            bar.AddArc(bx, by, br * 2.0f, br * 2.0f, 90, 180);
            bar.AddArc(bx, by + bh - br * 2.0f, br * 2.0f, br * 2.0f, 270,
                       180);
            bar.CloseFigure();
            g->FillPath(&accent, &bar);
        }

        const float pad = 14.0f * scale;
        const float close_w = 28.0f * scale;
        float text_x = card.X + pad + 8.0f * scale;
        if (t->icon) {
            const float isz = 32.0f * scale;
            const float iy = card.Y + (card.Height - isz) * 0.5f;
            g->DrawImage(t->icon.get(), text_x, iy, isz, isz);
            text_x += isz + 10.0f * scale;
        }
        const float text_w = card.X + card.Width - pad - close_w - text_x;

        t->close_rect.left =
            static_cast<LONG>(card.X + card.Width - close_w);
        t->close_rect.top = static_cast<LONG>(card.Y);
        t->close_rect.right = static_cast<LONG>(card.X + card.Width);
        t->close_rect.bottom = static_cast<LONG>(card.Y + 26.0f * scale);

        const std::wstring title = Utf8ToWide(t->opt.title);
        const std::wstring msg = Utf8ToWide(t->opt.message);
        const Gdiplus::RectF area(text_x, card.Y + pad * 0.7f, text_w,
                                  card.Height - pad);

        float ty = card.Y + pad * 0.7f;
        if (!title.empty()) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                static_cast<BYTE>(255 * alpha), 240, 242, 245));
            Gdiplus::RectF measured{};
            g->MeasureString(title.c_str(), -1, t->font_title.get(), area,
                             t->format.get(), &measured);
            const Gdiplus::RectF line(text_x, ty, text_w, measured.Height);
            g->DrawString(title.c_str(), -1, t->font_title.get(), line,
                          t->format.get(), &brush);
            ty += measured.Height + 3.0f * scale;
        }
        if (!msg.empty()) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                static_cast<BYTE>(255 * alpha), 168, 174, 185));
            Gdiplus::RectF measured{};
            g->MeasureString(msg.c_str(), -1, t->font_body.get(), area,
                             t->format.get(), &measured);
            const Gdiplus::RectF line(text_x, ty, text_w, measured.Height);
            g->DrawString(msg.c_str(), -1, t->font_body.get(), line,
                          t->format.get(), &brush);
        }

        // Close glyph: two crossing lines.
        {
            const float cx = card.X + card.Width - close_w * 0.5f;
            const float cy = card.Y + 13.0f * scale;
            const float arm = 5.0f * scale;
            Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(220 * alpha),
                                            150, 156, 168),
                             1.4f * scale);
            g->DrawLine(&pen, cx - arm, cy - arm, cx + arm, cy + arm);
            g->DrawLine(&pen, cx + arm, cy - arm, cx - arm, cy + arm);
        }

        POINT dst{static_cast<LONG>(std::lround(t->x)),
                  static_cast<LONG>(std::lround(t->y))};
        SIZE size{t->w, t->h};
        POINT src{0, 0};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        HDC screen = GetDC(nullptr);
        UpdateLayeredWindow(t->hwnd, screen, &dst, &size, t->memdc, &src, 0,
                            &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }

    void FrameTick(Toast* t) {
        const DWORD now = GetTickCount();
        const float dt = 16.0f / 1000.0f;

        if (!t->closing && t->anim_t < 1.0f) {
            t->anim_t = std::min(1.0f, t->anim_t + dt / kEnterSeconds);
        }
        if (!t->closing && t->opt.duration_ms > 0 &&
            now - t->born >= static_cast<DWORD>(t->opt.duration_ms)) {
            RequestClose(t);
        }

        const float k = 0.28f;
        t->x += (t->tx - t->x) * k;
        t->y += (t->ty - t->y) * k;
        const float target_alpha = t->closing ? 0.0f : 255.0f;
        t->alpha += (target_alpha - t->alpha) * k;

        if (t->closing && t->alpha < 3.0f) {
            DestroyWindow(t->hwnd);
            return;
        }
        RenderFrame(t);
    }

    static constexpr int kCardWidthDip = 360;
    static constexpr int kMargin = 16;
    static constexpr int kGap = 10;
    static constexpr float kEnterSeconds = 0.22f;

    std::once_flag start_once_;
    HANDLE ready_ = nullptr;
    DWORD thread_id_ = 0;
    std::thread thread_;
    std::vector<Toast*> toasts_;
};

namespace {

using Toast = ToastManager::Impl::Toast;

ToastManager::Impl*& ActiveImpl() {
    static thread_local ToastManager::Impl* impl = nullptr;
    return impl;
}

LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Toast* t =
        reinterpret_cast<Toast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    ToastManager::Impl* impl = ActiveImpl();

    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            t = static_cast<Toast*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(t));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_TIMER:
            if (wp == kTimerFrame && t && impl) {
                impl->FrameTick(t);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (t && impl) impl->RequestClose(t);
            return 0;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (t) {
                KillTimer(hwnd, kTimerFrame);
                if (impl) impl->RemoveToast(t);
                delete t;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

ToastManager::ToastManager() : impl_(std::make_unique<Impl>()) {}
ToastManager::~ToastManager() = default;

ToastManager& ToastManager::Instance() {
    static ToastManager instance;
    return instance;
}

void ToastManager::Show(ToastOptions options) {
    impl_->Show(std::move(options));
}

void ToastManager::Clear() {
    impl_->Clear();
}

void ToastManager::Shutdown() {
    impl_->Shutdown();
}

} // namespace XER::UI::Overlays

#else // !_WIN32

namespace XER::UI::Overlays {

struct ToastManager::Impl {
    void Show(ToastOptions) {}
    void Clear() {}
    void Shutdown() {}
};

ToastManager::ToastManager() : impl_(std::make_unique<Impl>()) {}
ToastManager::~ToastManager() = default;

ToastManager& ToastManager::Instance() {
    static ToastManager instance;
    return instance;
}

void ToastManager::Show(ToastOptions options) { impl_->Show(std::move(options)); }
void ToastManager::Clear() { impl_->Clear(); }
void ToastManager::Shutdown() { impl_->Shutdown(); }

} // namespace XER::UI::Overlays

#endif // _WIN32
