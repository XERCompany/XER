#ifndef XER_UI_CORE_WINDOW_H
#define XER_UI_CORE_WINDOW_H

#include <functional>
#include <memory>
#include <string>
#include <windows.h>

namespace XER::UI::Core {

// Forward declarations
namespace Widgets {
class Widget;
}

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

    // Callbacks
    void SetOnClose(Callback callback);
    void SetOnResize(std::function<void(int, int)> callback);

    // Properties
    void SetTitle(const std::string& title);
    std::string GetTitle() const;

    void SetSize(int width, int height);
    void GetSize(int* width, int* height) const;

    void SetPosition(int x, int y);
    void GetPosition(int* x, int* y) const;

    // Platform access (when direct WinAPI access needed)
    HWND GetHWND() const;

    bool IsVisible() const;
    bool IsClosed() const;

    // Prevent copying
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Core

#endif // XER_UI_CORE_WINDOW_H
