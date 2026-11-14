#ifndef XER_UI_WIDGETS_APP_TITLE_BAR_H
#define XER_UI_WIDGETS_APP_TITLE_BAR_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Modern IDE studio titlebar (Cursor / Windsurf / VS Code style):
// left workspace switcher + back/forward + menu bar, centered command/search
// pill with shortcut badges, right panel toggles + theme switch + avatar +
// accent action pill + native minimize/maximize/close window controls.
class AppTitleBar final : public Widget {
public:
    enum class WindowCommand { Minimize, ToggleMaximize, Close };

    struct MenuItem {
        std::string label;
        std::string shortcut;
        std::function<void()> action;
        bool enabled = true;
        bool checked = false;
        bool danger = false;
        // When true, adds a separator line above this item.
        bool separator_before = false;
    };

    struct Menu {
        std::string label;
        char mnemonic = 0;
        std::vector<MenuItem> items;
    };

    explicit AppTitleBar(std::string app_label = "App");

    // Workspace / app switcher (e.g. "Hub", "IDE", "Studio").
    void SetWorkspaceLabel(std::string label);
    const std::string& GetWorkspaceLabel() const { return workspace_label_; }
    void SetWorkspaceItems(std::vector<MenuItem> items);

    // Optional back/forward navigation arrows on the left.
    void SetNavActions(bool can_go_back, bool can_go_forward,
                       std::function<void()> on_back,
                       std::function<void()> on_forward);

    // Top menu bar items (File, Edit, Filter, View, Models, Help, ...).
    void SetMenus(std::vector<Menu> menus);
    void ClearMenus();

    // Centered command/search pill (Ctrl+K / Ctrl+P style).
    void SetCenterCommand(std::string placeholder,
                          std::function<void()> on_activate,
                          bool show_shortcuts = true);
    void SetCenterCommandValue(std::string value);
    const std::string& GetCenterCommandValue() const { return center_value_; }

    // Right-side layout panel toggles (left sidebar, right inspector).
    void SetLeftPanelToggle(bool visible, bool active,
                            std::function<void()> on_toggle);
    void SetRightPanelToggle(bool visible, bool active,
                             std::function<void()> on_toggle);

    // Light/dark theme quick switcher in the header.
    void SetThemeToggle(bool is_dark, std::function<void(bool)> on_theme_changed);

    // User avatar with dropdown menu (e.g. account, settings, sign out).
    void SetAvatar(std::string display_name, std::vector<MenuItem> menu_items = {});

    // Bright accent pill button on the right (e.g. "Sync Civitai", "Run Job").
    void SetActionPill(std::string text, std::function<void()> on_click,
                       bool active = false);
    void ClearActionPill();

    // Direct window control callback (defaults to posting WM_* to HWND).
    void SetOnWindowCommand(std::function<void(WindowCommand)> callback);

    // Cached maximize state so the header can swap minimize/maximize glyphs.
    // The owner updates this from Window::IsWindowMaximized() on resize/tick.
    void SetMaximized(bool maximized) {
        if (maximized_ != maximized) {
            maximized_ = maximized;
            Invalidate();
        }
    }
    bool GetMaximized() const { return maximized_; }

    // True when `position` (widget-local DIP coordinates) hits an interactive
    // control in the titlebar; false over empty drag regions so Window can
    // treat them as HTCAPTION.
    bool IsInteractiveAt(Layout::Point position) const;
    bool HasInteractiveAt(Layout::Point position) const;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds,
                const Theme::Tokens& tokens) override;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    enum class HitZone {
        None,
        Workspace,
        NavBack,
        NavForward,
        Menu,
        CenterCommand,
        LeftPanel,
        RightPanel,
        ThemeToggle,
        Avatar,
        ActionPill,
        Minimize,
        Maximize,
        Close
    };

    struct MenuEntry {
        std::string label;
        char mnemonic = 0;
        std::vector<MenuItem> items;
        Layout::Rect rect{};
        float width = 0.0f;
    };

    HitZone HitTest(Layout::Point p) const;
    void OpenMenuOverlay(int menu_index, Layout::Point anchor);
    void ExecuteWindowCommand(WindowCommand cmd);
    void InvalidateInteractive(Layout::Point p);

    void DrawVectorIcon(Rendering::Renderer& r, const Theme::Tokens& tokens,
                        const char* icon_name, Layout::Rect box,
                        Theme::Color color, bool active = false) const;

    std::string app_label_;
    std::string workspace_label_;
    std::vector<MenuItem> workspace_items_;
    bool show_nav_ = false;
    bool can_go_back_ = false;
    bool can_go_forward_ = false;
    std::function<void()> on_back_;
    std::function<void()> on_forward_;
    std::vector<MenuEntry> menus_;
    int hovered_menu_index_ = -1;

    std::string center_placeholder_ = "Search or run command...";
    std::string center_value_;
    std::function<void()> on_center_activate_;
    bool show_center_shortcuts_ = true;

    bool show_left_panel_ = false;
    bool left_panel_active_ = false;
    std::function<void()> on_left_panel_toggle_;
    bool show_right_panel_ = false;
    bool right_panel_active_ = false;
    std::function<void()> on_right_panel_toggle_;

    bool show_theme_toggle_ = false;
    bool is_dark_theme_ = true;
    std::function<void(bool)> on_theme_changed_;

    std::string avatar_name_ = "User";
    std::string avatar_initials_ = "U";
    std::vector<MenuItem> avatar_items_;

    std::string action_pill_text_;
    bool action_pill_active_ = false;
    std::function<void()> on_action_pill_;

    std::function<void(WindowCommand)> on_window_command_;
    bool maximized_ = false;

    // Cached interactive rectangles (in widget-local DIP coordinates).
    Layout::Rect bounds_{};
    Layout::Rect workspace_rect_{};
    Layout::Rect nav_back_rect_{};
    Layout::Rect nav_fwd_rect_{};
    Layout::Rect center_bar_rect_{};
    Layout::Rect left_panel_rect_{};
    Layout::Rect right_panel_rect_{};
    Layout::Rect theme_toggle_rect_{};
    Layout::Rect avatar_rect_{};
    Layout::Rect action_pill_rect_{};
    Layout::Rect minimize_rect_{};
    Layout::Rect maximize_rect_{};
    Layout::Rect close_rect_{};

    HitZone hovered_zone_ = HitZone::None;
    HitZone pressed_zone_ = HitZone::None;
    Motion::Tween hover_anim_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_APP_TITLE_BAR_H
