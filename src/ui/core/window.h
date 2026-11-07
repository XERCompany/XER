#ifndef XER_UI_CORE_WINDOW_H
#define XER_UI_CORE_WINDOW_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <windows.h>

#include "../theme/theme.h"

namespace XER::UI::Widgets {
class Widget;
}

namespace XER::UI::Core {

/**
 * Window - Main window class with small interface
 *
 * Deep module hiding WinAPI complexity:
 * - Create(): Window creation and registration
 * - Show()/Hide(): Visibility control
 * - Close(): Window destruction
 * - SetContent(): Widget tree attachment
 *
 * All WinAPI details (WNDCLASS, CreateWindowEx, message handling) hidden.
 */
class Window {
public:
    using Callback = std::function<void()>;

    Window();
    ~Window();

    // Small interface - core operations only
    bool Create(const std::string& title, int width, int height);
    void Show();
    void Hide();
    void Close();

    // Content management
    void SetContent(std::shared_ptr<Widgets::Widget> root);
    std::shared_ptr<Widgets::Widget> GetContent() const;
    void SetTheme(Theme::Variant variant);
    // Empty after a successful paint; otherwise returns the most recent native paint error.
    std::string GetLastRenderError() const;

    // Schedules a full client-area repaint on the next WM_PAINT.
    void Invalidate();
    // Invokes `callback` on the UI thread every `interval_ms` while the window
    // exists. Pass an empty callback to stop. Repaints only occur when widgets
    // report changes, so an idle callback is nearly free.
    void SetFrameCallback(std::function<void()> callback, std::uint32_t interval_ms = 33);

    // Callbacks
    void SetOnClose(Callback callback);
    void SetOnResize(std::function<void(int, int)> callback);

    // Properties
    void SetTitle(const std::string& title);
    std::string GetTitle() const;

    // When true, WM_CLOSE hides the window instead of destroying it, so it
    // can be shown again with Show(). Used for secondary/tool windows.
    void SetCloseHides(bool hides);

    void SetSize(int width, int height);
    void GetSize(int* width, int* height) const;

    // Window size limits in pixels (outer window size). 0 = unconstrained.
    // Enforced via WM_GETMINMAXINFO while the window is being resized.
    void SetSizeLimits(int min_width, int min_height, int max_width, int max_height);

    void SetPosition(int x, int y);
    void GetPosition(int* x, int* y) const;

    // Platform access (when direct WinAPI access needed)
    HWND GetHWND() const;

    bool IsVisible() const;
    bool IsClosed() const;

    // Internal message handler (called by static WindowProc)
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleNativeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Prevent copying
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Core

#endif // XER_UI_CORE_WINDOW_H
