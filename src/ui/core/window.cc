#include "window.h"
#include "../widgets/widget.h"
#include <stdexcept>
#include <unordered_map>

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
        return window->HandleMessage(msg, wParam, lParam);
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
        , closed_(false) {}

    ~Impl() {
        if (hwnd_) {
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
        root_widget_ = root;
        // TODO: Layout and render when window is shown
    }

    std::shared_ptr<Widgets::Widget> GetContent() const {
        return root_widget_;
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
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CLOSE:
                if (on_close_) {
                    on_close_();
                }
                closed_ = true;
                {
                    std::lock_guard<std::mutex> lock(g_window_map_mutex);
                    g_window_map.erase(hwnd_);
                }
                DestroyWindow(hwnd_);
                hwnd_ = nullptr;
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            case WM_SIZE:
                if (on_resize_) {
                    int new_width = LOWORD(lParam);
                    int new_height = HIWORD(lParam);
                    width_ = new_width;
                    height_ = new_height;
                    on_resize_(new_width, new_height);
                }
                // TODO: Relayout content
                return 0;

            case WM_PAINT:
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd_, &ps);
                // TODO: Render content
                EndPaint(hwnd_, &ps);
                return 0;

            default:
                return DefWindowProc(hwnd_, msg, wParam, lParam);
        }
    }

    void SetWindow(Window* window) {
        window_ = window;
    }

private:
    HWND hwnd_;
    std::string title_;
    int width_;
    int height_;
    int x_;
    int y_;
    bool visible_;
    bool closed_;

    Window* window_;
    std::shared_ptr<Widgets::Widget> root_widget_;

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

} // namespace XER::UI::Core
