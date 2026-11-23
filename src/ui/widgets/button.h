#ifndef XER_UI_WIDGETS_BUTTON_H
#define XER_UI_WIDGETS_BUTTON_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/context_menu.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class ButtonStyle {
    Primary,
    Secondary,
    Outline,
    // shadcn "ghost": transparent until hover.
    Ghost,
    // shadcn "destructive": danger fill for irreversible actions.
    Destructive
};

// views-style button sizing (shadcn sm/lg): changes height and padding
// only — the label keeps the body font.
enum class ButtonSize {
    Small,    // 28 DIP — dense toolbars, table rows.
    Default,  // 36 DIP — the existing button height.
    Large     // 44 DIP — prominent touch-friendly actions.
};

// Which side of the label the icon sits on (views::LabelButton image
// placement). Trailing suits "open externally" affordances.
enum class IconPosition {
    Leading,
    Trailing
};

class Button final : public Widget {
public:
    explicit Button(std::string text = {}, ButtonStyle style = ButtonStyle::Secondary);
    ~Button() override;

    void SetText(std::string text);
    const std::string& GetText() const;
    std::string GetDebugText() const override { return text_; }
    void SetOnClick(std::function<void()> callback);
    void SetStyle(ButtonStyle style);

    // views-style density variant (shadcn sm/lg sizes).
    void SetSize(ButtonSize size);
    ButtonSize GetSize() const { return size_; }

    // ---- optional icon ----
    // src/ui has no icon pack of its own, so icon lookup goes through a
    // resolver the host app installs once at boot (e.g. the video
    // editor wires it to its raster IconPack). Signature matches the
    // app's draw helper: draws `icon_name` rasterized at `px` tinted
    // `tint` centered inside `box`, returns false when unavailable.
    using IconResolver =
        std::function<bool(Rendering::Renderer& renderer,
                           const char* icon_name, int px,
                           Theme::Color tint, Layout::Rect box)>;
    static void SetIconResolver(IconResolver resolver);
    // Logical icon name drawn left of the label (centered when the
    // label is empty), ~16 DIP, tinted with the label color. Empty /
    // no resolver keeps the button text-only — full back-compat.
    void SetIcon(std::string icon_name);
    const std::string& GetIcon() const { return icon_; }
    // views::LabelButton-style leading/trailing icon placement.
    void SetIconPosition(IconPosition position);
    IconPosition GetIconPosition() const { return icon_position_; }
    // Busy state: draws a spinner inside the button, keeps its width and
    // swallows clicks until cleared.
    void SetLoading(bool loading);
    bool IsLoading() const { return loading_; }

    // ---- QPushButton-style checkable state ----
    // A checkable Button keeps the button chrome (any ButtonStyle) but
    // latches: every activation flips checked_ and fires the callback.
    // Distinct from the Toggle widget, which has its own shadcn chrome.
    void SetCheckable(bool checkable);
    bool IsCheckable() const { return checkable_; }
    void SetChecked(bool checked);
    bool IsChecked() const { return checked_; }
    void SetOnCheckedChanged(std::function<void(bool)> callback);

    // ---- dialog default affordance ----
    // QPushButton::setDefault / views::MdTextButton's default state:
    // draws an accent ring marking the dialog's default action. The
    // host/dialog routes Enter to it; AutoDefault buttons mark
    // themselves default on activation (Qt behavior).
    void SetDefault(bool is_default);
    bool IsDefault() const { return is_default_; }
    void SetAutoDefault(bool auto_default);
    bool IsAutoDefault() const { return auto_default_; }

    // Menu-item style shortcut affordance ("Ctrl+S") drawn muted after
    // the label. Purely visual — the host maps the key itself.
    void SetShortcut(std::string shortcut);
    const std::string& GetShortcut() const { return shortcut_; }

    // QPushButton::setMenu / views::MenuButton: draws a chevron at the
    // trailing edge; activation opens a ContextMenu anchored at the
    // button's bottom-left instead of firing on_click_. Items mirror
    // ContextMenu::Item; pass to SetMenu again to refresh a live menu.
    void SetMenu(std::vector<ContextMenu::Item> items);
    void ClearMenu();
    bool HasMenu() const { return !menu_items_.empty(); }
    bool IsMenuOpen() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void Activate();
    void OpenMenu();
    float HeightForSize() const;

    std::string text_;
    std::string icon_;
    std::string shortcut_;
    std::function<void()> on_click_;
    ButtonStyle style_;
    ButtonSize size_ = ButtonSize::Default;
    IconPosition icon_position_ = IconPosition::Leading;
    bool loading_ = false;
    bool checkable_ = false;
    bool checked_ = false;
    bool is_default_ = false;
    bool auto_default_ = false;
    std::function<void(bool)> on_checked_changed_;
    std::vector<ContextMenu::Item> menu_items_;
    std::weak_ptr<Widget> menu_;

    // Motion state (springs/tweens per the XER motion rules).
    Motion::Spring press_;       // 0..1 while the pointer is held down
    Motion::Tween hover_t_;      // eased hover wash
    float spinner_angle_ = 0.0f; // loading spinner rotation
    bool held_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_BUTTON_H
