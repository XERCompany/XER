#ifndef XER_UI_CORE_WINDOW_H
#define XER_UI_CORE_WINDOW_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <windows.h>

#include "../layout/geometry.h"
#include "../theme/theme.h"

namespace XER::UI::Widgets {
class Widget;
}

namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::UI::Core {

class UiEventBus;

// Per-painted-frame render diagnostics. Updated on every successful WM_PAINT;
// use it to quantify "broken frames" (frame time, repaint coverage) instead of
// guessing from screenshots.
struct RenderStats {
    std::uint64_t frames = 0;        // completed paints since window creation
    double last_frame_ms = 0.0;      // last paint incl. Present/vsync wait
    double avg_frame_ms = 0.0;       // exponential moving average (alpha 0.1)
    float last_damage_pct = 100.0f;  // % of client area repainted last frame
    bool last_frame_full = true;     // last paint covered (nearly) the client
};

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
class Window : public std::enable_shared_from_this<Window> {
public:
    using Callback = std::function<void()>;

    Window();
    ~Window();

    // Small interface - core operations only
    bool Create(const std::string& title, int width, int height);
    void Show();
    void Hide();
    void Close();
    void Minimize();
    void ToggleMaximize();
    bool IsWindowMaximized() const;

    // Frameless IDE-style window chrome: removes the native caption bar while
    // preserving DWM shadow, Aero Snap, resize borders, and rounded corners.
    // `is_interactive` returns true for DIP client points inside the titlebar
    // that belong to interactive widgets (menus, search bar, window buttons)
    // so empty titlebar areas act as native HTCAPTION drag/snap regions.
    void SetCustomTitleBar(bool enabled, float height_dip = 38.0f);
    bool HasCustomTitleBar() const;
    void SetTitleBarHitTest(std::function<bool(Layout::Point)> is_interactive);

    // Content management
    void SetContent(std::shared_ptr<Widgets::Widget> root);
    std::shared_ptr<Widgets::Widget> GetContent() const;
    void SetTheme(Theme::Variant variant);
    Theme::Variant GetTheme() const;
    // Loads custom design tokens from a JSON theme file
    // (theme/theme_loader.h: colors + metrics over a Light/Dark base)
    // and repaints. Returns false and fills `error` on parse failure;
    // the previous theme is kept.
    bool SetThemeFile(const std::string& path, std::string* error = nullptr);
    // Empty after a successful paint; otherwise returns the most recent native paint error.
    std::string GetLastRenderError() const;
    // Frame-time and damage-coverage counters for the last painted frames.
    RenderStats GetRenderStats() const;

    // Injects a custom Renderer implementation (e.g. a stream renderer
    // that forwards frames to xer_engine_ui_render.exe). Call before or
    // after Create(); an existing default renderer is replaced. The
    // window takes ownership. Passing nullptr restores the default
    // Direct2D renderer on the next frame.
    void SetRenderer(std::unique_ptr<Rendering::Renderer> renderer);

    // UI event system shared with the WidgetTree: event filters +
    // listeners observe/mutate input events before dispatch, render
    // hooks run around each painted frame. The window keeps the bus
    // alive; the WidgetTree sees it via SetEventBus.
    void SetEventBus(std::shared_ptr<UiEventBus> bus);
    std::shared_ptr<UiEventBus> GetEventBus() const;

    // Schedules a full client-area repaint on the next WM_PAINT.
    void Invalidate();
    // Invokes `callback` on the UI thread every `interval_ms` while the window
    // exists. Pass an empty callback to stop. Repaints only occur when widgets
    // report changes, so an idle callback is nearly free.
    void SetFrameCallback(std::function<void()> callback, std::uint32_t interval_ms = 33);

    // Callbacks
    void SetOnClose(Callback callback);
    void SetOnResize(std::function<void(int, int)> callback);

    // Additional close observers, invoked while the window is still
    // alive during WM_CLOSE teardown (and once more if the Window object
    // is destroyed without a prior WM_CLOSE). Used by DevTools to shut
    // down inspector windows synchronously with their target.
    // Returns a handle for RemoveCloseHook.
    std::uint64_t AddCloseHook(Callback callback);
    void RemoveCloseHook(std::uint64_t id);

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
