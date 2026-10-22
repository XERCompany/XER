#ifndef XER_UI_WIDGETS_WIDGET_H
#define XER_UI_WIDGETS_WIDGET_H

#include <memory>
#include <functional>

namespace XER::UI {

// Forward declarations
namespace Rendering {
class Renderer;
}

namespace Layout {
struct Rect;
struct Size;
struct Point;
}

namespace Widgets {

/**
 * Event structure for widget events
 */
struct Event {
    enum class Type {
        MouseMove,
        MouseDown,
        MouseUp,
        Click,
        KeyDown,
        KeyUp,
        Focus,
        Blur
    };

    Type type;
    int x;
    int y;
    int key_code;
    bool is_ctrl;
    bool is_shift;
    bool is_alt;
};

/**
 * Widget - Base class for all UI widgets
 *
 * Deep module with small interface:
 * - Render(): Draw the widget
 * - Layout(): Calculate size and position
 * - HandleEvent(): Process user input
 *
 * All widget-specific complexity hidden in derived classes.
 */
class Widget {
public:
    virtual ~Widget() = default;

    // Small interface - core operations only
    virtual void Render(Rendering::Renderer& renderer) = 0;
    virtual void Layout(const Layout::Rect& bounds) = 0;
    virtual bool HandleEvent(const Event& event) = 0;

    // Size hints for layout system
    virtual Layout::Size GetMinSize() const = 0;
    virtual Layout::Size GetPreferredSize() const = 0;
    virtual Layout::Size GetMaxSize() const = 0;

    // Visibility and enabled state
    void SetVisible(bool visible);
    bool IsVisible() const;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // Parent-child relationships
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
    Layout::Rect bounds_;
    bool visible_ = true;
    bool enabled_ = true;

    std::weak_ptr<Widget> parent_;
    std::vector<std::shared_ptr<Widget>> children_;
};

} // namespace XER::UI::Widgets

} // namespace XER::UI

#endif // XER_UI_WIDGETS_WIDGET_H
