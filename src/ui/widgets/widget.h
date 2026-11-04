#ifndef XER_UI_WIDGETS_WIDGET_H
#define XER_UI_WIDGETS_WIDGET_H

#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"

namespace XER::UI {

namespace Rendering {
class Renderer;
}

namespace Theme {
struct Tokens;
}

namespace Widgets {

enum class KeyCode {
    None,
    Tab,
    Enter,
    Space,
    Backspace,
    Delete,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Home,
    End,
    Escape
};

enum class MouseButton {
    None,
    Left,
    Middle,
    Right
};

struct Event {
    enum class Type {
        MouseMove,
        MouseDown,
        MouseUp,
        Click,
        KeyDown,
        KeyUp,
        TextInput,
        Focus,
        Blur
    };

    Type type = Type::MouseMove;
    Layout::Point position;
    MouseButton mouse_button = MouseButton::None;
    KeyCode key = KeyCode::None;
    std::string text;
    bool is_ctrl = false;
    bool is_shift = false;
    bool is_alt = false;
};

class Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget() = default;

    virtual void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) = 0;
    virtual void Layout(const Layout::Rect& bounds) = 0;
    // Theme-aware layout. Widgets that need theme metrics (e.g. Card padding)
    // override this; the default delegates to Layout(bounds) so widgets that
    // ignore theme metrics keep working unchanged.
    virtual void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens);
    virtual bool HandleEvent(const Event& event) = 0;

    virtual Layout::Size GetMinSize() const = 0;
    virtual Layout::Size GetPreferredSize() const = 0;
    virtual Layout::Size GetMaxSize() const = 0;

    void SetVisible(bool visible);
    bool IsVisible() const;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetFocusable(bool focusable);
    bool IsFocusable() const;

    void SetFocused(bool focused);
    bool IsFocused() const;

    void SetParent(std::shared_ptr<Widget> parent);
    std::shared_ptr<Widget> GetParent() const;

    void AddChild(std::shared_ptr<Widget> child);
    void RemoveChild(std::shared_ptr<Widget> child);
    const std::vector<std::shared_ptr<Widget>>& GetChildren() const;

protected:
    Widget() = default;

    Layout::Rect GetBounds() const;
    void SetBounds(const Layout::Rect& bounds);

    void Invalidate();
    void RequestLayout();

private:
    friend class WidgetTree;

    void ClearFocusedSubtree();

    Layout::Rect bounds_;
    bool visible_ = true;
    bool enabled_ = true;
    bool focusable_ = false;
    bool focused_ = false;

    std::weak_ptr<Widget> parent_;
    std::vector<std::shared_ptr<Widget>> children_;
};

} // namespace Widgets
} // namespace XER::UI

#endif // XER_UI_WIDGETS_WIDGET_H
