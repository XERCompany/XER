#include "window.h"

#include "../widgets/widget.h"
#include "../widgets/widget_tree.h"
#include "../rendering/windows/direct2d_renderer.h"
#include "../platform/windows/input_translator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include <windowsx.h>

namespace XER::UI::Core {

// Static window class registration
static const wchar_t* WINDOW_CLASS_NAME = L"XER_UI_WINDOW";

// Global map to associate HWND with Window instances
static std::unordered_map<HWND, Window*> g_window_map;
static std::mutex g_window_map_mutex;

// Window procedure - forwards to Window instance
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* window = nullptr;

    // Associate HWND with Window instance on WM_NCCREATE
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = static_cast<Window*>(cs->lpCreateParams);

        std::lock_guard<std::mutex> lock(g_window_map_mutex);
        g_window_map[hwnd] = window;
    } else {
        std::lock_guard<std::mutex> lock(g_window_map_mutex);
        auto it = g_window_map.find(hwnd);
        if (it != g_window_map.end()) {
            window = it->second;
        }
    }

    if (window) {
        const auto result = window->HandleNativeMessage(hwnd, msg, wParam, lParam);
        if (msg == WM_NCDESTROY) {
            std::lock_guard<std::mutex> lock(g_window_map_mutex);
            g_window_map.erase(hwnd);
        }
        return result;
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
        int len = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        std::wstring wtitle(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wtitle[0], len);

        // Create window
        hwnd_ = CreateWindowExW(
            0,                              // Extended style
            WINDOW_CLASS_NAME,              // Class name
            wtitle.c_str(),                  // Window title
            WS_OVERLAPPEDWINDOW,            // Window style
            x_, y_,                         // Position
            width_, height_,                // Size
            nullptr,                        // Parent window
            nullptr,                        // Menu
            GetModuleHandle(nullptr),       // Instance
            window_                         // Creation parameter (this)
        );

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

    void SetTitle(const std::string& title) {
        title_ = title;
        if (hwnd_) {
            int len = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
            std::wstring wtitle(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wtitle[0], len);
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
                if (on_close_) {
                    on_close_();
                }
                closed_ = true;
                renderer_.reset();
                DestroyWindow(message_hwnd);
                hwnd_ = nullptr;
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

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
                    InvalidateRect(message_hwnd, nullptr, FALSE);
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

private:
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
                static bool logged_paint_once = false;
                if (!logged_paint_once) {
                    logged_paint_once = true;
                    RECT cr{};
                    GetClientRect(hwnd, &cr);
                    fprintf(stderr,
                            "[DEBUG-paint] dpi=%u client=%ldx%ld root_children=%zu\n",
                            static_cast<unsigned>(dpi_),
                            static_cast<long>(cr.right),
                            static_cast<long>(cr.bottom),
                            root_widget_->GetChildren().size());
                }
                widget_tree_.Render(*renderer_, theme_.GetTokens());
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

    Window* window_;
    std::shared_ptr<Widgets::Widget> root_widget_;
    Widgets::WidgetTree widget_tree_;
    Theme::Provider theme_;
    Platform::Windows::InputTranslator input_translator_;
    std::unique_ptr<Rendering::Windows::Direct2DRenderer> renderer_;
    std::string last_render_error_;

    Callback on_close_;
    std::function<void(int, int)> on_resize_;
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

void Window::SetTitle(const std::string& title) {
    impl_->SetTitle(title);
}

std::string Window::GetTitle() const {
    return impl_->GetTitle();
}

void Window::SetSize(int width, int height) {
    impl_->SetSize(width, height);
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
