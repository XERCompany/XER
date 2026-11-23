#include "window.h"

#include "../widgets/widget.h"
#include "../widgets/widget_tree.h"
#include "ui_events.h"
#include "../rendering/windows/direct2d_renderer.h"
#include "../rendering/renderer_provider.h"
#include "../platform/windows/input_translator.h"
#include "application.h"
#include "helper/string_utils.h"
#include "theme/theme_loader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace XER::UI::Core {

// Static window class registration
static const wchar_t* WINDOW_CLASS_NAME = L"XER_UI_WINDOW";

// Number of live top-level windows. The app message loop quits only when
// the last window is destroyed, so secondary windows (DevTools, dialogs)
// can come and go without killing the application.
static std::atomic<int> g_open_windows{0};

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
        try {
            return window->HandleNativeMessage(hwnd, msg, wParam, lParam);
        } catch (const std::exception& e) {
            fprintf(stderr, "WndProc exception msg=%u: %s\n", msg, e.what());
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        } catch (...) {
            fprintf(stderr, "WndProc exception msg=%u: unknown\n", msg);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
        // Run close hooks for destruction paths that bypass WM_CLOSE
        // (e.g. the Window object being dropped while the app runs).
        RunCloseHooks();
        if (hwnd_) {
            renderer_.reset();
            // Detach the WndProc from this Window BEFORE DestroyWindow.
            // unique_ptr nulls its pointer before running ~Impl, so the
            // WM_DESTROY/WM_NCDESTROY messages dispatched below would
            // otherwise be routed into HandleNativeMessage on a dead impl_.
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
        }
    }

    // Detaches and invokes every registered close hook. The vector is
    // swapped out first so hooks may safely register/remove hooks.
    void RunCloseHooks() {
        auto hooks = std::exchange(close_hooks_, {});
        for (auto& [id, cb] : hooks) {
            (void)id;
            if (cb) cb();
        }
    }

    std::uint64_t AddCloseHook(Callback cb) {
        const std::uint64_t id = next_hook_id_++;
        close_hooks_.emplace_back(id, std::move(cb));
        return id;
    }

    void RemoveCloseHook(std::uint64_t id) {
        std::erase_if(close_hooks_,
                      [id](const auto& entry) { return entry.first == id; });
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
            ApplyCustomFrame();
            UpdateFrameTimer();
            g_open_windows.fetch_add(1, std::memory_order_release);
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
        // Picking a builtin variant drops any file/custom token set so
        // theme toggles always restore a real variant look.
        theme_.ClearTokens();
        theme_.SetVariant(variant);
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void SetTokens(Theme::Tokens tokens) {
        // Custom sets keep the current variant's metric base unless the
        // caller pre-baked metrics in — the recorded variant drives
        // GetTheme() reporting and any later ClearTheme().
        theme_.SetTokens(std::move(tokens), theme_.GetVariant());
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void ClearTheme() {
        theme_.ClearTokens();
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    Theme::Variant GetTheme() const {
        return theme_.GetVariant();
    }

    std::string GetLastRenderError() const {
        return last_render_error_;
    }

    RenderStats GetRenderStats() const {
        return render_stats_;
    }

    void SetCustomTitleBar(bool enabled, float height_dip) {
        if (enabled && !(height_dip > 0.0f)) {
            height_dip = 38.0f;
        }
        custom_title_bar_ = enabled;
        title_bar_height_dip_ = enabled ? height_dip : 38.0f;
        ApplyCustomFrame();
    }

    bool HasCustomTitleBar() const {
        return custom_title_bar_;
    }

    void SetTitleBarHitTest(std::function<bool(Layout::Point)> hit_test) {
        title_bar_hit_test_ = std::move(hit_test);
    }

    void Minimize() {
        if (hwnd_ != nullptr) {
            ShowWindow(hwnd_, SW_MINIMIZE);
        }
    }

    void ToggleMaximize() {
        if (hwnd_ == nullptr) {
            return;
        }
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        LayoutContent(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    bool IsWindowMaximized() const {
        return hwnd_ != nullptr && IsZoomed(hwnd_) != 0;
    }

    void SetOnClose(Callback callback) {
        on_close_ = callback;
    }

    void SetCloseGuard(std::function<bool()> guard) {
        close_guard_ = std::move(guard);
    }

    void SetOnResize(std::function<void(int, int)> callback) {
        on_resize_ = callback;
    }

    void Invalidate() {
        // Drop pending dirty flags so the explicit invalidate below is the
        // single source of the next repaint. A pending layout request must
        // run first — discarding it would leave stale geometry forever.
        widget_tree_.ConsumeDirtyPaint();
        if (widget_tree_.ConsumeLayoutRequest() && hwnd_ != nullptr) {
            LayoutContent(hwnd_);
        }
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void SetRenderer(std::unique_ptr<Rendering::Renderer> renderer) {
        renderer_ = std::move(renderer);
    }

    void SetEventBus(std::shared_ptr<UiEventBus> bus) {
        event_bus_ = std::move(bus);
        widget_tree_.SetEventBus(event_bus_.get());
    }

    bool SetThemeFile(const std::string& path, std::string* error) {
        std::string err;
        if (!Theme::ThemeLoader::ApplyToProvider(theme_, path, &err)) {
            if (error != nullptr) {
                *error = std::move(err);
            }
            return false;
        }
        Invalidate();
        return true;
    }

    std::shared_ptr<UiEventBus> GetEventBus() const { return event_bus_; }

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
                // Unsaved-changes veto: the guard runs before ANY
                // teardown so a cancel leaves the window fully alive.
                if (close_guard_ && !close_guard_()) {
                    return 0;
                }
                if (close_hides_) {
                    ShowWindow(message_hwnd, SW_HIDE);
                    visible_ = false;
                    return 0;
                }
                KillFrameTimer();
                closed_ = true;
                // Close hooks run while the window is still alive so
                // observers (e.g. DevTools) can react synchronously.
                RunCloseHooks();
                renderer_.reset();
                DestroyWindow(message_hwnd);
                hwnd_ = nullptr;
                // on_close_ runs last: it may drop the last shared_ptr
                // owning this Window (e.g. DevTools registry cleanup),
                // destroying Impl — no member access is allowed after it.
                if (on_close_) {
                    on_close_();
                }
                return 0;

            case WM_DESTROY: {
                KillFrameTimer();
                // Quit the message loop only when the last top-level
                // window goes away; guard on IsRunning so windows torn
                // down after Application::Shutdown don't repost WM_QUIT.
                if (g_open_windows.fetch_sub(1, std::memory_order_acq_rel) <= 1 &&
                    Application::Instance().IsRunning()) {
                    PostQuitMessage(0);
                }
                return 0;
            }

            case WM_TIMER:
                if (wParam == kFrameTimerId) {
                    // XER_UI_PERF: measure the whole per-tick cost — the
                    // app callback, widget Tick and the invalidation
                    // flush all run on the UI thread, so a heavy tick
                    // delays every queued paint exactly as a slow paint
                    // would (visible as constant micro-jitter).
                    const auto tick_start =
                        std::chrono::steady_clock::now();
                    if (frame_callback_) {
                        frame_callback_();
                    }
                    const auto app_cb_done =
                        std::chrono::steady_clock::now();
                    widget_tree_.Tick(
                        static_cast<double>(frame_interval_ms_) * 0.001);
                    FlushDirtyState(message_hwnd);
                    if (perf_hud_) {
                        const double cb_ms =
                            std::chrono::duration<double, std::milli>(
                                app_cb_done - tick_start).count();
                        const double tick_ms =
                            std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() -
                                tick_start).count();
                        tick_cost_avg_ = tick_cost_avg_ * 0.9 + tick_ms * 0.1;
                        tick_cb_avg_ = tick_cb_avg_ * 0.9 + cb_ms * 0.1;
                        if (tick_ms > tick_cost_max_) {
                            tick_cost_max_ = tick_ms;
                        }
                    }
                    return 0;
                }
                return DefWindowProcW(message_hwnd, msg, wParam, lParam);

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

            case WM_CAPTURECHANGED:
                // Capture was lost without a MouseUp (another window stole
                // it, or the button was released while unfocused). Settle
                // any in-progress drag instead of leaving it stuck.
                widget_tree_.CancelPointerCapture();
                FlushDirtyState(message_hwnd);
                return 0;

            case WM_MOUSEWHEEL: {
                // lParam carries screen coordinates for wheel messages.
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (!ScreenToClient(message_hwnd, &point)) {
                    return DefWindowProcW(message_hwnd, msg, wParam, lParam);
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

            case WM_NCCALCSIZE: {
                if (custom_title_bar_ && wParam == TRUE && hwnd_ != nullptr) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    if (params != nullptr) {
                        RECT rect = params->rgrc[0];
                        if (IsZoomed(hwnd_)) {
                            const int cx_frame =
                                GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi_);
                            const int cy_frame =
                                GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi_);
                            const int cx_padded =
                                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
                            const int cy_padded =
                                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
                            rect.left += cx_frame + cx_padded;
                            rect.right -= cx_frame + cx_padded;
                            rect.top += cy_frame + cy_padded;
                            rect.bottom -= cy_frame + cy_padded;
                            params->rgrc[0] = rect;
                        }
                        return 0;
                    }
                }
                return DefWindowProcW(message_hwnd, msg, wParam, lParam);
            }

            case WM_NCHITTEST: {
                if (custom_title_bar_ && hwnd_ != nullptr &&
                    message_hwnd != nullptr) {
                    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    if (ScreenToClient(message_hwnd, &pt)) {
                        const float scale = 96.0f / static_cast<float>(dpi_);
                        const float dip_x = static_cast<float>(pt.x) * scale;
                        const float dip_y = static_cast<float>(pt.y) * scale;
                        RECT rect{};
                        if (GetClientRect(message_hwnd, &rect)) {
                            const float w =
                                static_cast<float>(rect.right) * scale;
                            const float h =
                                static_cast<float>(rect.bottom) * scale;
                            constexpr float kResizeBorder = 6.0f;
                            if (!IsZoomed(message_hwnd) &&
                                dip_x >= 0.0f && dip_x <= w &&
                                dip_y >= 0.0f && dip_y <= h) {
                                const bool left = dip_x < kResizeBorder;
                                const bool right = dip_x > w - kResizeBorder;
                                const bool top = dip_y < kResizeBorder;
                                const bool bottom = dip_y > h - kResizeBorder;
                                if (top && left) return HTTOPLEFT;
                                if (top && right) return HTTOPRIGHT;
                                if (bottom && left) return HTBOTTOMLEFT;
                                if (bottom && right) return HTBOTTOMRIGHT;
                                if (left) return HTLEFT;
                                if (right) return HTRIGHT;
                                if (top) return HTTOP;
                                if (bottom) return HTBOTTOM;
                            }
                            if (dip_y >= 0.0f &&
                                dip_y < title_bar_height_dip_) {
                                // While a modal overlay (menu / select / dialog)
                                // is open, route titlebar clicks to the client
                                // area so outside clicks dismiss the overlay
                                // instead of dragging the window.
                                if (widget_tree_.GetOverlay() != nullptr) {
                                    return HTCLIENT;
                                }
                                if (title_bar_hit_test_ &&
                                    title_bar_hit_test_(
                                        Layout::Point{dip_x, dip_y})) {
                                    return HTCLIENT;
                                }
                                widget_tree_.ClearHover();
                                FlushDirtyState(message_hwnd);
                                return HTCAPTION;
                            }
                        }
                    }
                }
                return DefWindowProcW(message_hwnd, msg, wParam, lParam);
            }

            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                if (info == nullptr) {
                    return DefWindowProcW(message_hwnd, msg, wParam, lParam);
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
                // Losing focus mid-drag means the button-up may never reach
                // us — settle drag state before dispatching Blur.
                if (msg == WM_KILLFOCUS) {
                    widget_tree_.CancelPointerCapture();
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
                    // Release capture only after the MouseUp was delivered:
                    // ReleaseCapture() posts WM_CAPTURECHANGED synchronously,
                    // and cancelling capture first would swallow the click.
                    // Only the left button owns capture (set on LBUTTONDOWN);
                    // releasing on right/middle ups used to drop it mid-drag.
                    if (msg == WM_LBUTTONUP && GetCapture() == message_hwnd) {
                        ReleaseCapture();
                    }
                    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
                        return DefWindowProcW(message_hwnd, msg, wParam, lParam);
                    }
                    if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
                        return DefWindowProcW(message_hwnd, msg, wParam, lParam);
                    }
                    return 0;
                }
                return DefWindowProcW(message_hwnd, msg, wParam, lParam);
            }

            case WM_NCDESTROY: {
                renderer_.reset();
                const LRESULT result = DefWindowProcW(message_hwnd, msg, wParam, lParam);
                if (hwnd_ == message_hwnd) {
                    hwnd_ = nullptr;
                }
                return result;
            }

            default:
                return DefWindowProcW(message_hwnd, msg, wParam, lParam);
        }
    }

    void SetWindow(Window* window) {
        window_ = window;
    }

    // When true, WM_CLOSE hides the window instead of closing it — used by
    // secondary windows that should not terminate the shared message loop.
    bool close_hides_ = false;

    void ApplyCustomFrame() {
        if (hwnd_ == nullptr) {
            return;
        }
        if (custom_title_bar_) {
            // Extend 1px of glass into the top edge so DWM keeps the native
            // shadow while WM_NCCALCSIZE removes the caption area.
            const MARGINS margins{0, 0, 1, 0};
            DwmExtendFrameIntoClientArea(hwnd_, &margins);
        } else {
            const MARGINS margins{0, 0, 0, 0};
            DwmExtendFrameIntoClientArea(hwnd_, &margins);
        }
        // Windows 11 rounded corners (DWMWCP_ROUND = 2) for both modes.
        DWORD corner = 2;
        DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                              &corner, sizeof(corner));
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

private:
    // Repaints only when widgets reported changes since the last flush.
    // Paint-only changes invalidate each reported damage rect (converted
    // DIP->px) instead of the whole client area; Windows coalesces them
    // into the update region while the render pass culls per-rect.
    void FlushDirtyState(HWND hwnd) {
        if (hwnd == nullptr) {
            return;
        }
        const bool layout_requested = widget_tree_.ConsumeLayoutRequest();
        const bool paint_requested = widget_tree_.ConsumeDirtyPaint();
        const auto damage = widget_tree_.ConsumeDamageRect();
        const bool full_damage = widget_tree_.ConsumeFullDamage();
        if (layout_requested) {
            LayoutContent(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            damage_rects_.clear();
            return;
        }
        if (!paint_requested && !damage.valid && !full_damage) {
            return;
        }
        if (full_damage || damage.rects.empty()) {
            InvalidateRect(hwnd, nullptr, FALSE);
            damage_rects_.clear();
            return;
        }
        RECT client{};
        if (!GetClientRect(hwnd, &client)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            damage_rects_.clear();
            return;
        }
        const float dip_to_px = static_cast<float>(dpi_) / 96.0f;
        for (const Layout::Rect& dr : damage.rects) {
            RECT rc = {
                static_cast<LONG>(std::floor(dr.x * dip_to_px)),
                static_cast<LONG>(std::floor(dr.y * dip_to_px)),
                static_cast<LONG>(
                    std::ceil((dr.x + dr.width) * dip_to_px)),
                static_cast<LONG>(
                    std::ceil((dr.y + dr.height) * dip_to_px)),
            };
            rc.left = std::max<LONG>(rc.left, client.left);
            rc.top = std::max<LONG>(rc.top, client.top);
            rc.right = std::min<LONG>(rc.right, client.right);
            rc.bottom = std::min<LONG>(rc.bottom, client.bottom);
            if (rc.right <= rc.left || rc.bottom <= rc.top) {
                continue;  // this rect fell entirely outside the client
            }
            InvalidateRect(hwnd, &rc, FALSE);
            // Keep the DIP rect so the next WM_PAINT can cull against the
            // same scattered list instead of the rcPaint bounding union.
            Widgets::WidgetTree::MergeDamageRect(&damage_rects_, dr);
        }
        // If every rect clamped outside the client area, no InvalidateRect
        // was issued — nothing to repaint, matching the old early-return.
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
            // WM_TIMER defaults to the 15.6 ms system tick: a 33 ms
            // request lands at 31/47 ms steps and every per-tick
            // invalidation (playhead, meters, monitor frame) arrives
            // with visible cadence jitter. NtSetTimerResolution is
            // resolved dynamically — always present on supported
            // Windows, no winmm import needed.
            using NtSetTimerResolutionFn =
                LONG(WINAPI*)(ULONG, BOOLEAN, PULONG);
            static const auto set_res =
                reinterpret_cast<NtSetTimerResolutionFn>(GetProcAddress(
                    GetModuleHandleW(L"ntdll.dll"),
                    "NtSetTimerResolution"));
            if (set_res != nullptr && !timer_res_raised_) {
                ULONG current = 0;
                if (set_res(5000, TRUE, &current) >= 0) {
                    timer_res_raised_ = true;  // 0.5 ms units
                }
            }
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
        (void)hdc;  // presentation goes through the DXGI swapchain, not GDI
        try {
            if (!perf_env_checked_) {
                perf_env_checked_ = true;
                if (const char* v = std::getenv("XER_UI_PERF")) {
                    perf_hud_ =
                        v[0] != '\0' &&
                        !(v[0] == '0' && v[1] == '\0') &&
                        !(v[0] == 'o' && v[1] == 'f' && v[2] == 'f');
                }
            }
            const auto frame_start = std::chrono::steady_clock::now();
            float damage_pct = 100.0f;
            const auto background = theme_.GetTokens().background;
            if (root_widget_ == nullptr || !root_widget_->IsVisible()) {
                // No widget tree: present a solid clear through the swapchain
                // (GDI cannot draw on a flip-model swapchain surface).
                if (renderer_ == nullptr) {
                    // XER_UI_RENDERER picks the backend; the provider
                    // chain always lands on a working renderer.
                    renderer_ =
                        Rendering::CreateRendererFromEnv(hwnd);
                }
                if (renderer_ == nullptr) {
                    renderer_ = std::make_unique<Rendering::Windows::Direct2DRenderer>(hwnd);
                }
                renderer_->SetDpi(dpi_);
                renderer_->SetClearColor(background);
                RECT client{};
                if (GetClientRect(hwnd, &client)) {
                    const float scale = 96.0f / static_cast<float>(dpi_);
                    renderer_->BeginFrame(Layout::Rect{
                        0.0f, 0.0f,
                        static_cast<float>(client.right) * scale,
                        static_cast<float>(client.bottom) * scale});
                    renderer_->EndFrame();
                }
            } else {
                if (renderer_ == nullptr) {
                    renderer_ =
                        Rendering::CreateRendererFromEnv(hwnd);
                }
                if (renderer_ == nullptr) {
                    renderer_ = std::make_unique<Rendering::Windows::Direct2DRenderer>(hwnd);
                }
                renderer_->SetDpi(dpi_);
                renderer_->SetClearColor(background);
                // XER_UI_PERF=1: push last frame's stats so the renderer
                // can overlay them (HUD reads always describe the
                // previously completed frame).
                if (perf_env_checked_ && perf_hud_) {
                    hud_info_.frame_ms = render_stats_.last_frame_ms;
                    hud_info_.avg_frame_ms = render_stats_.avg_frame_ms;
                    hud_info_.damage_pct = render_stats_.last_damage_pct;
                    hud_info_.draw_calls = last_counters_.draw_calls;
                    hud_info_.text_calls = last_counters_.text_calls;
                    hud_info_.image_calls = last_counters_.image_calls;
                    hud_info_.widgets_painted =
                        last_widget_stats_.painted;
                    hud_info_.widgets_culled = last_widget_stats_.culled;
                    renderer_->SetPerfHud(&hud_info_);
                } else if (perf_env_checked_) {
                    renderer_->SetPerfHud(nullptr);
                }
                // rcPaint (px) -> widget coords (DIP): the retained canvas
                // keeps last frame's pixels outside this region.
                const float px_to_dip = 96.0f / static_cast<float>(dpi_);
                const Layout::Rect damage{
                    static_cast<float>(paint_rect.left) * px_to_dip,
                    static_cast<float>(paint_rect.top) * px_to_dip,
                    static_cast<float>(paint_rect.right - paint_rect.left) *
                        px_to_dip,
                    static_cast<float>(paint_rect.bottom - paint_rect.top) *
                        px_to_dip};
                // Consume the damage list accumulated by FlushDirtyState.
                // It drives the render cull only when rcPaint is partial:
                // a full-client update region means some path (layout,
                // resize, explicit Invalidate) invalidated everything, so
                // the scattered list would wrongly cull live widgets.
                std::vector<Layout::Rect> cull_rects =
                    std::exchange(damage_rects_, {});
                RECT client_px{};
                const bool client_ok = GetClientRect(hwnd, &client_px) != 0;
                const bool covers_full_client =
                    !client_ok ||
                    (paint_rect.left <= client_px.left &&
                     paint_rect.top <= client_px.top &&
                     paint_rect.right >= client_px.right &&
                     paint_rect.bottom >= client_px.bottom);
                const std::vector<Layout::Rect>* render_cull =
                    (!covers_full_client && !cull_rects.empty())
                        ? &cull_rects
                        : nullptr;
                if (event_bus_ != nullptr &&
                    event_bus_->HasRenderHooks(
                        UiEventBus::RenderPhase::kBeforeFrame)) {
                    UiRenderFrame frame{};
                    frame.bounds = root_widget_->GetBounds();
                    frame.damage = damage;
                    frame.damage_valid = true;
                    frame.last_frame_ms = render_stats_.last_frame_ms;
                    event_bus_->NotifyRender(
                        UiEventBus::RenderPhase::kBeforeFrame, frame);
                }
                const auto render_start =
                    std::chrono::steady_clock::now();
                widget_tree_.Render(*renderer_, theme_.GetTokens(),
                                    render_cull);
                last_render_ms_ =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - render_start)
                        .count();
                last_counters_ = renderer_->TakeFrameCounters();
                last_widget_stats_ = widget_tree_.LastFrameWidgetStats();
                if (event_bus_ != nullptr) {
                    UiRenderFrame frame{};
                    frame.bounds = root_widget_->GetBounds();
                    frame.damage = damage;
                    frame.damage_valid = true;
                    frame.last_frame_ms = render_stats_.last_frame_ms;
                    event_bus_->NotifyRender(
                        UiEventBus::RenderPhase::kAfterFrame, frame);
                }
                // Damage coverage: what this paint actually repainted vs
                // the client area. When a scattered rect list drove the
                // cull, sum those rects — rcPaint is only the bounding
                // union of the update region and overstates scattered
                // partial repaints (meter + playhead ≈ fullscreen union).
                // ~100% means a full repaint (resize, layout, explicit
                // Invalidate); small values mean a regional update.
                if (client_ok) {
                    const float client_w =
                        (client_px.right - client_px.left) * px_to_dip;
                    const float client_h =
                        (client_px.bottom - client_px.top) * px_to_dip;
                    const float client_area = client_w * client_h;
                    if (client_area > 0.0f) {
                        float damaged_area =
                            damage.width * damage.height;
                        if (render_cull != nullptr) {
                            float rects_area = 0.0f;
                            for (const Layout::Rect& r : cull_rects) {
                                rects_area += r.width * r.height;
                            }
                            damaged_area =
                                std::min(damaged_area, rects_area);
                        }
                        damage_pct = std::clamp(
                            damaged_area / client_area * 100.0f,
                            0.0f, 100.0f);
                    }
                }
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
            const double frame_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - frame_start).count();
            render_stats_.avg_frame_ms = render_stats_.frames == 0
                ? frame_ms
                : render_stats_.avg_frame_ms * 0.9 + frame_ms * 0.1;
            render_stats_.frames += 1;
            render_stats_.last_frame_ms = frame_ms;
            render_stats_.last_damage_pct = damage_pct;
            render_stats_.last_frame_full = damage_pct >= 99.5f;
            // Perf log line every 300 frames — lets harnesses verify frame
            // cost from stderr without reading pixels off the HUD.
            if (perf_hud_ && render_stats_.frames % 300 == 0) {
                std::fprintf(
                    stderr,
                    "[XER UI perf] frames=%llu last=%.1fms avg=%.1fms "
                    "render=%.1fms present=%.1fms "
                    "dmg=%.0f%% draws=%u txt=%u img=%u widgets=%u(cull %u) "
                    "tick=%.1f/%.1f cb=%.1f\n",
                    static_cast<unsigned long long>(render_stats_.frames),
                    render_stats_.last_frame_ms,
                    render_stats_.avg_frame_ms,
                    last_render_ms_ - widget_tree_.LastEndFrameMs(),
                    widget_tree_.LastEndFrameMs(),
                    static_cast<double>(damage_pct),
                    last_counters_.draw_calls,
                    last_counters_.text_calls,
                    last_counters_.image_calls,
                    last_widget_stats_.painted,
                    last_widget_stats_.culled,
                    tick_cost_avg_, tick_cost_max_, tick_cb_avg_);
                tick_cost_max_ = 0.0;
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
    std::unique_ptr<Rendering::Renderer> renderer_;
    std::shared_ptr<UiEventBus> event_bus_;
    std::string last_render_error_;
    RenderStats render_stats_;
    // XER_UI_PERF diagnostics state (env read lazily on the first paint).
    bool perf_env_checked_ = false;
    bool perf_hud_ = false;
    Rendering::Renderer::PerfHudInfo hud_info_{};
    Rendering::Renderer::FrameCounters last_counters_{};
    Widgets::WidgetTree::FrameWidgetStats last_widget_stats_{};
    // DIP-space damage rects accumulated by FlushDirtyState since the last
    // paint — consumed by Paint() as the render-cull list so scattered
    // invalidations don't cull against the rcPaint bounding union.
    std::vector<Layout::Rect> damage_rects_;

    bool custom_title_bar_ = false;
    float title_bar_height_dip_ = 38.0f;
    std::function<bool(Layout::Point)> title_bar_hit_test_;

    Callback on_close_;
    std::function<bool()> close_guard_;
    std::function<void(int, int)> on_resize_;
    std::function<void()> frame_callback_;
    std::vector<std::pair<std::uint64_t, Callback>> close_hooks_;
    std::uint64_t next_hook_id_ = 1;
    std::uint32_t frame_interval_ms_ = 33;
    bool frame_timer_active_ = false;
    bool timer_res_raised_ = false;  // NtSetTimerResolution succeeded
    double last_render_ms_ = 0.0;    // widget Render() wall time
    // Per-tick UI-thread cost diagnostics (XER_UI_PERF): avg/max of the
    // whole WM_TIMER handler, and the app frame_callback_ share of it.
    double tick_cost_avg_ = 0.0;
    double tick_cost_max_ = 0.0;
    double tick_cb_avg_ = 0.0;
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

void Window::Minimize() {
    impl_->Minimize();
}

void Window::ToggleMaximize() {
    impl_->ToggleMaximize();
}

bool Window::IsWindowMaximized() const {
    return impl_->IsWindowMaximized();
}

void Window::SetCustomTitleBar(bool enabled, float height_dip) {
    impl_->SetCustomTitleBar(enabled, height_dip);
}

bool Window::HasCustomTitleBar() const {
    return impl_->HasCustomTitleBar();
}

void Window::SetTitleBarHitTest(std::function<bool(Layout::Point)> is_interactive) {
    impl_->SetTitleBarHitTest(std::move(is_interactive));
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

Theme::Variant Window::GetTheme() const {
    return impl_->GetTheme();
}

bool Window::SetThemeFile(const std::string& path, std::string* error) {
    return impl_->SetThemeFile(path, error);
}

void Window::SetTokens(Theme::Tokens tokens) {
    impl_->SetTokens(std::move(tokens));
}

void Window::ClearTheme() {
    impl_->ClearTheme();
}

std::string Window::GetLastRenderError() const {
    return impl_->GetLastRenderError();
}

RenderStats Window::GetRenderStats() const {
    return impl_->GetRenderStats();
}

void Window::SetOnClose(Callback callback) {
    impl_->SetOnClose(callback);
}

void Window::SetCloseGuard(std::function<bool()> guard) {
    impl_->SetCloseGuard(std::move(guard));
}

void Window::SetOnResize(std::function<void(int, int)> callback) {
    impl_->SetOnResize(callback);
}

std::uint64_t Window::AddCloseHook(Callback callback) {
    return impl_->AddCloseHook(std::move(callback));
}

void Window::RemoveCloseHook(std::uint64_t id) {
    impl_->RemoveCloseHook(id);
}

void Window::Invalidate() {
    impl_->Invalidate();
}

void Window::SetRenderer(std::unique_ptr<Rendering::Renderer> renderer) {
    impl_->SetRenderer(std::move(renderer));
}

void Window::SetEventBus(std::shared_ptr<UiEventBus> bus) {
    impl_->SetEventBus(std::move(bus));
}

std::shared_ptr<UiEventBus> Window::GetEventBus() const {
    return impl_->GetEventBus();
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
    if (impl_ == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return impl_->HandleMessage(hwnd, msg, wParam, lParam);
}

} // namespace XER::UI::Core
