#include "window.h"

#include "../widgets/widget.h"
#include "../widgets/widget_tree.h"
#include "../rendering/windows/direct2d_renderer.h"
#include "../platform/windows/input_translator.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace XER::UI::Core {

// Static window class registration
static const wchar_t* WINDOW_CLASS_NAME = L"XER_UI_WINDOW";

// Window procedure - the Window instance is stored in GWLP_USERDATA so lookup
// stays a single pointer read with no locking on the message path.
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* window = nullptr;

    if (msg == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = static_cast<Window*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Window*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window != nullptr) {
        return window->HandleNativeMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Private implementation
class Window::Impl {
public:
    Impl()
        : hwnd_(nullptr)
        , title_("XER Window")
        , width_(800)
        , height_(600)
        , x_(CW_USEDEFAULT)
        , y_(CW_USEDEFAULT)
        , visible_(false)
        , closed_(false)
        , dpi_(USER_DEFAULT_SCREEN_DPI)
        , min_width_(0)
        , min_height_(0)
        , max_width_(0)
        , max_height_(0)
        , window_(nullptr) {}

    ~Impl() {
        if (hwnd_) {
            renderer_.reset();
            DestroyWindow(hwnd_);
        }
    }

    bool Create(const std::string& title, int width, int height) {
        if (hwnd_) {
            return false; // Already created
        }

        title_ = title;
        width_ = width;
        height_ = height;

        // Register window class if not already registered
        static bool class_registered = false;
        if (!class_registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandle(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = WINDOW_CLASS_NAME;

            if (!RegisterClassExW(&wc)) {
                return false;
            }
            class_registered = true;
        }

        // Convert title to wide string
        const std::wstring wtitle = XER::Helper::String::Utf8ToWide(title);

        // Create window
        hwnd_ = CreateWindowExW(
            0,                              // Extended style
            WINDOW_CLASS_NAME,              // Class name
            wtitle.c_str(),                  // Window title
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, // Window style
            x_, y_,                         // Position
            width_, height_,                // Size
            nullptr,                        // Parent window
            nullptr,                        // Menu
            GetModuleHandle(nullptr),       // Instance
            window_                         // Creation parameter (this)
        );

        if (hwnd_ != nullptr) {
            // Dark title bar / caption buttons to match the studio theme.
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                  &dark, sizeof(dark));
            UpdateFrameTimer();
        }
        return hwnd_ != nullptr;
    }

    void Show() {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_SHOW);
            visible_ = true;
        }
    }

    void Hide() {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_HIDE);
            visible_ = false;
        }
    }

    void Close() {
        if (hwnd_) {
            PostMessage(hwnd_, WM_CLOSE, 0, 0);
        }
    }

    void SetContent(std::shared_ptr<Widgets::Widget> root) {
        root_widget_ = std::move(root);
        widget_tree_.SetRoot(root_widget_);
        LayoutContent(hwnd_);
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    std::shared_ptr<Widgets::Widget> GetContent() const {
        return root_widget_;
    }

    void SetTheme(Theme::Variant variant) {
        theme_.SetVariant(variant);
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    std::string GetLastRenderError() const {
        return last_render_error_;
    }

    void SetOnClose(Callback callback) {
        on_close_ = callback;
    }

    void SetOnResize(std::function<void(int, int)> callback) {
        on_resize_ = callback;
    }

    void Invalidate() {
        // Drop pending dirty flags so the explicit invalidate below is the
        // single source of the next repaint.
        widget_tree_.ConsumeDirtyPaint();
        widget_tree_.ConsumeLayoutRequest();
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void SetFrameCallback(std::function<void()> callback, std::uint32_t interval_ms) {
        frame_callback_ = std::move(callback);
        frame_interval_ms_ = interval_ms == 0 ? 33 : interval_ms;
        UpdateFrameTimer();
    }

    void SetTitle(const std::string& title) {
        title_ = title;
        if (hwnd_) {
            const std::wstring wtitle = XER::Helper::String::Utf8ToWide(title);
            SetWindowTextW(hwnd_, wtitle.c_str());
        }
    }

    std::string GetTitle() const {
        return title_;
    }

    void SetSize(int width, int height) {
        width_ = width;
        height_ = height;
        if (hwnd_) {
            SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                        SWP_NOMOVE | SWP_NOZORDER);
        }
    }

    void SetSizeLimits(int min_width, int min_height, int max_width, int max_height) {
        min_width_ = min_width;
        min_height_ = min_height;
        max_width_ = max_width;
        max_height_ = max_height;
        if (hwnd_ != nullptr) {
            // Clamp the current size immediately if it violates the new limits.
            RECT rect{};
            GetWindowRect(hwnd_, &rect);
            const int w = rect.right - rect.left;
            const int h = rect.bottom - rect.top;
            int new_w = w;
            int new_h = h;
            if (min_width_ > 0) new_w = std::max(new_w, min_width_);
            if (min_height_ > 0) new_h = std::max(new_h, min_height_);
            if (max_width_ > 0) new_w = std::min(new_w, max_width_);
            if (max_height_ > 0) new_h = std::min(new_h, max_height_);
            if (new_w != w || new_h != h) {
                SetWindowPos(hwnd_, nullptr, 0, 0, new_w, new_h,
                            SWP_NOMOVE | SWP_NOZORDER);
            }
        }
    }

    void GetSize(int* width, int* height) const {
        if (width) *width = width_;
        if (height) *height = height_;
    }

    void SetPosition(int x, int y) {
        x_ = x;
        y_ = y;
        if (hwnd_) {
            SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER);
        }
    }

    void GetPosition(int* x, int* y) const {
        if (x) *x = x_;
        if (y) *y = y_;
    }

    HWND GetHWND() const {
        return hwnd_;
    }

    bool IsVisible() const {
        return visible_;
    }

    bool IsClosed() const {
        return closed_;
    }

    // Message handler (called from WindowProc)
    LRESULT HandleMessage(HWND message_hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (hwnd_ == nullptr) {
            hwnd_ = message_hwnd;
            dpi_ = GetDpiForWindow(message_hwnd);
            if (dpi_ == 0) {
                dpi_ = USER_DEFAULT_SCREEN_DPI;
            }
        }
        switch (msg) {
            case WM_CLOSE:
                if (close_hides_) {
                    ShowWindow(message_hwnd, SW_HIDE);
                    visible_ = false;
                    return 0;
                }
                KillFrameTimer();
                if (on_close_) {
                    on_close_();
                }
                closed_ = true;
                renderer_.reset();
                DestroyWindow(message_hwnd);
                hwnd_ = nullptr;
                return 0;

            case WM_DESTROY:
                KillFrameTimer();
                PostQuitMessage(0);
                return 0;

            case WM_TIMER:
                if (wParam == kFrameTimerId) {
                    if (frame_callback_) {
                        frame_callback_();
                    }
                    FlushDirtyState(message_hwnd);
                    return 0;
                }
                return DefWindowProc(message_hwnd, msg, wParam, lParam);

            case WM_SIZE:
            {
                const int new_width = LOWORD(lParam);
                const int new_height = HIWORD(lParam);
                width_ = new_width;
                height_ = new_height;
                LayoutContent(message_hwnd, new_width, new_height);
                if (on_resize_) {
                    on_resize_(new_width, new_height);
                }
                InvalidateRect(message_hwnd, nullptr, FALSE);
                return 0;
            }

            case WM_DPICHANGED: {
                const UINT new_dpi = HIWORD(wParam);
                if (new_dpi != 0) {
                    dpi_ = new_dpi;
                    if (renderer_ != nullptr) {
                        renderer_->SetDpi(dpi_);
                    }
                }

                const auto* suggested_rect = reinterpret_cast<const RECT*>(lParam);
                if (suggested_rect != nullptr &&
                    !SetWindowPos(
                        message_hwnd,
                        nullptr,
                        suggested_rect->left,
                        suggested_rect->top,
                        suggested_rect->right - suggested_rect->left,
                        suggested_rect->bottom - suggested_rect->top,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    )) {
                    ReportRenderError("SetWindowPos failed while applying WM_DPICHANGED");
                }

                LayoutContent(message_hwnd);
                InvalidateRect(message_hwnd, nullptr, FALSE);
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT paint{};
                HDC hdc = BeginPaint(message_hwnd, &paint);
                Paint(message_hwnd, hdc, paint.rcPaint);
                EndPaint(message_hwnd, &paint);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_MOUSELEAVE:
                widget_tree_.ClearHover();
                FlushDirtyState(message_hwnd);
                return 0;

            case WM_MOUSEWHEEL: {
                // lParam carries screen coordinates for wheel messages.
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (!ScreenToClient(message_hwnd, &point)) {
                    return DefWindowProc(message_hwnd, msg, wParam, lParam);
                }
                Widgets::Event event;
                event.type = Widgets::Event::Type::MouseWheel;
                const float scale = 96.0f / static_cast<float>(dpi_);
                event.position = Layout::Point(
                    static_cast<float>(point.x) * scale,
                    static_cast<float>(point.y) * scale
                );
                event.wheel_delta = GET_WHEEL_DELTA_WPARAM(wParam);
                event.is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                event.is_ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                event.is_alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                widget_tree_.HandleEvent(event);
                FlushDirtyState(message_hwnd);
                return 0;
            }

            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                if (info == nullptr) {
                    return DefWindowProc(message_hwnd, msg, wParam, lParam);
                }
                if (min_width_ > 0) {
                    info->ptMinTrackSize.x = min_width_;
                }
                if (min_height_ > 0) {
                    info->ptMinTrackSize.y = min_height_;
                }
                if (max_width_ > 0) {
                    info->ptMaxTrackSize.x = max_width_;
                }
                if (max_height_ > 0) {
                    info->ptMaxTrackSize.y = max_height_;
                }
                return 0;
            }

            case WM_LBUTTONDOWN:
                SetFocus(message_hwnd);
                SetCapture(message_hwnd);
                [[fallthrough]];
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_MOUSEMOVE:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_SETFOCUS:
            case WM_KILLFOCUS: {
                if (msg == WM_MOUSEMOVE) {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = message_hwnd;
                    TrackMouseEvent(&tme);
                }
                if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP) {
                    if (GetCapture() == message_hwnd) {
                        ReleaseCapture();
                    }
                }
                auto event = input_translator_.Translate(msg, wParam, lParam);
                if (event) {
                    if (event->type == Widgets::Event::Type::MouseMove ||
                        event->type == Widgets::Event::Type::MouseDown ||
                        event->type == Widgets::Event::Type::MouseUp) {
                        const float scale = 96.0f / static_cast<float>(dpi_);
                        event->position.x *= scale;
                        event->position.y *= scale;
                    }
                    widget_tree_.HandleEvent(*event);
                    FlushDirtyState(message_hwnd);
                    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
                        return DefWindowProc(message_hwnd, msg, wParam, lParam);
                    }
                    if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
                        return DefWindowProc(message_hwnd, msg, wParam, lParam);
                    }
                    return 0;
                }
                return DefWindowProc(message_hwnd, msg, wParam, lParam);
            }

            case WM_NCDESTROY: {
                renderer_.reset();
                const LRESULT result = DefWindowProc(message_hwnd, msg, wParam, lParam);
                if (hwnd_ == message_hwnd) {
                    hwnd_ = nullptr;
                }
                return result;
            }

            default:
                return DefWindowProc(message_hwnd, msg, wParam, lParam);
        }
    }

    void SetWindow(Window* window) {
        window_ = window;
    }

    // When true, WM_CLOSE hides the window instead of closing it — used by
    // secondary windows that should not terminate the shared message loop.
    bool close_hides_ = false;

private:
    // Repaints only when widgets reported changes since the last flush.
    void FlushDirtyState(HWND hwnd) {
        if (hwnd == nullptr) {
            return;
        }
        const bool layout_requested = widget_tree_.ConsumeLayoutRequest();
        const bool paint_requested = widget_tree_.ConsumeDirtyPaint();
        if (layout_requested) {
            LayoutContent(hwnd);
        }
        if (layout_requested || paint_requested) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void UpdateFrameTimer() {
        if (hwnd_ == nullptr) {
            return;
        }
        if (frame_timer_active_) {
            KillTimer(hwnd_, kFrameTimerId);
            frame_timer_active_ = false;
        }
        if (frame_callback_) {
            SetTimer(hwnd_, kFrameTimerId, frame_interval_ms_, nullptr);
            frame_timer_active_ = true;
        }
    }

    void KillFrameTimer() {
        if (frame_timer_active_ && hwnd_ != nullptr) {
            KillTimer(hwnd_, kFrameTimerId);
        }
        frame_timer_active_ = false;
    }

    void LayoutContent(HWND hwnd, int client_width = -1, int client_height = -1) {
        if (hwnd == nullptr || root_widget_ == nullptr) {
            return;
        }

        if (client_width < 0 || client_height < 0) {
            RECT client_rect{};
            if (!GetClientRect(hwnd, &client_rect)) {
                return;
            }
            client_width = client_rect.right;
            client_height = client_rect.bottom;
        }
        const float scale = 96.0f / static_cast<float>(dpi_);
        widget_tree_.Layout(
            Layout::Rect(
                0.0f,
                0.0f,
                static_cast<float>(client_width) * scale,
                static_cast<float>(client_height) * scale
            ),
            theme_.GetTokens()
        );
    }

    void Paint(HWND hwnd, HDC hdc, const RECT& paint_rect) {
        try {
            const auto background = theme_.GetTokens().background;
            if (root_widget_ == nullptr || !root_widget_->IsVisible()) {
                FillBackground(hdc, paint_rect, background);
            } else {
                if (renderer_ == nullptr) {
                    renderer_ = std::make_unique<Rendering::Windows::Direct2DRenderer>(hwnd);
                }
                renderer_->SetDpi(dpi_);
                renderer_->SetClearColor(background);
                widget_tree_.Render(*renderer_, theme_.GetTokens());
                // Flush pending child-window paints (the NodeGraph canvas
                // HWND) synchronously inside this WM_PAINT, right after the
                // parent surface commits. Without this the two HWNDs can
                // present one frame apart — the visible "half-frame"
                // flicker. Only children with a non-empty update region
                // repaint, so an idle canvas costs nothing.
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_ALLCHILDREN | RDW_UPDATENOW);
                // Clear any stray dirty flags raised during rendering so a
                // misbehaving widget cannot trigger a paint loop.
                widget_tree_.ConsumeDirtyPaint();
                widget_tree_.ConsumeLayoutRequest();
            }
            last_render_error_.clear();
        } catch (const std::exception& error) {
            renderer_.reset();
            ReportRenderError(error.what());
        } catch (...) {
            renderer_.reset();
            ReportRenderError("Unknown native renderer failure");
        }
    }

    void FillBackground(HDC hdc, const RECT& paint_rect, Theme::Color color) {
        const auto channel = [](float value) {
            return static_cast<BYTE>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        HBRUSH brush = CreateSolidBrush(RGB(
            channel(color.r),
            channel(color.g),
            channel(color.b)
        ));
        if (brush == nullptr) {
            throw std::runtime_error("CreateSolidBrush failed while painting an empty window");
        }
        const int painted = FillRect(hdc, &paint_rect, brush);
        DeleteObject(brush);
        if (painted == 0) {
            throw std::runtime_error("FillRect failed while painting an empty window");
        }
    }

    void ReportRenderError(const char* message) {
        last_render_error_ = message;
        std::string output = "XER UI native rendering error: ";
        output += message;
        output += "\n";
        OutputDebugStringA(output.c_str());
        fprintf(stderr, "[DEBUG-paint] render error: %s\n", message);
    }

    HWND hwnd_;
    std::string title_;
    int width_;
    int height_;
    int x_;
    int y_;
    bool visible_;
    bool closed_;
    UINT dpi_;
    int min_width_;
    int min_height_;
    int max_width_;
    int max_height_;

    Window* window_;
    std::shared_ptr<Widgets::Widget> root_widget_;
    Widgets::WidgetTree widget_tree_;
    Theme::Provider theme_;
    Platform::Windows::InputTranslator input_translator_;
    std::unique_ptr<Rendering::Windows::Direct2DRenderer> renderer_;
    std::string last_render_error_;

    Callback on_close_;
    std::function<void(int, int)> on_resize_;
    std::function<void()> frame_callback_;
    std::uint32_t frame_interval_ms_ = 33;
    bool frame_timer_active_ = false;
    static constexpr UINT_PTR kFrameTimerId = 0x4552; // 'ER'
};

// Window implementation
Window::Window() : impl_(std::make_unique<Impl>()) {
    impl_->SetWindow(this);
}

Window::~Window() = default;

bool Window::Create(const std::string& title, int width, int height) {
    return impl_->Create(title, width, height);
}

void Window::Show() {
    impl_->Show();
}

void Window::Hide() {
    impl_->Hide();
}

void Window::Close() {
    impl_->Close();
}

void Window::SetContent(std::shared_ptr<Widgets::Widget> root) {
    impl_->SetContent(root);
}

std::shared_ptr<Widgets::Widget> Window::GetContent() const {
    return impl_->GetContent();
}

void Window::SetTheme(Theme::Variant variant) {
    impl_->SetTheme(variant);
}

std::string Window::GetLastRenderError() const {
    return impl_->GetLastRenderError();
}

void Window::SetOnClose(Callback callback) {
    impl_->SetOnClose(callback);
}

void Window::SetOnResize(std::function<void(int, int)> callback) {
    impl_->SetOnResize(callback);
}

void Window::Invalidate() {
    impl_->Invalidate();
}

void Window::SetFrameCallback(std::function<void()> callback, std::uint32_t interval_ms) {
    impl_->SetFrameCallback(std::move(callback), interval_ms);
}

void Window::SetTitle(const std::string& title) {
    impl_->SetTitle(title);
}

std::string Window::GetTitle() const {
    return impl_->GetTitle();
}

void Window::SetCloseHides(bool hides) {
    impl_->close_hides_ = hides;
}

void Window::SetSize(int width, int height) {
    impl_->SetSize(width, height);
}

void Window::SetSizeLimits(int min_width, int min_height, int max_width, int max_height) {
    impl_->SetSizeLimits(min_width, min_height, max_width, max_height);
}

void Window::GetSize(int* width, int* height) const {
    impl_->GetSize(width, height);
}

void Window::SetPosition(int x, int y) {
    impl_->SetPosition(x, y);
}

void Window::GetPosition(int* x, int* y) const {
    impl_->GetPosition(x, y);
}

HWND Window::GetHWND() const {
    return impl_->GetHWND();
}

bool Window::IsVisible() const {
    return impl_->IsVisible();
}

bool Window::IsClosed() const {
    return impl_->IsClosed();
}

LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    return impl_->HandleMessage(impl_->GetHWND(), msg, wParam, lParam);
}

LRESULT Window::HandleNativeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return impl_->HandleMessage(hwnd, msg, wParam, lParam);
}

} // namespace XER::UI::Core
