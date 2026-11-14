#ifndef XER_UI_WIDGETS_RESIZABLE_H
#define XER_UI_WIDGETS_RESIZABLE_H

#include <functional>
#include <memory>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Resizable — container with draggable splitters between children
// (react-resizable-panels style). Children are laid out horizontally or
// vertically with user-adjustable sizes persisted via callbacks.
class Resizable final : public Widget {
public:
    enum class Direction { Horizontal, Vertical };
    enum class HandlePosition { Start, Between, End };

    struct Panel {
        std::shared_ptr<Widget> widget;
        float size = 0.0f;  // fraction 0..1 (0 = auto/equal)
        float min_size = 0.0f;
        bool collapsible = true;
    };

    static std::shared_ptr<Resizable> Create(Direction dir = Direction::Horizontal);

    void AddPanel(Panel panel);
    void ClearPanels();
    void SetDirection(Direction dir);
    Direction GetDirection() const { return dir_; }
    void SetHandleSize(float px);
    void SetOnLayout(std::function<void(const std::vector<float>& sizes)> cb);
    void SetCollapsed(int index, bool collapsed);
    bool IsCollapsed(int index) const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Resizable(Direction dir);
    float HandleHit(const Layout::Rect& bounds, const Layout::Point& p) const;
    void RecomputeSizes(const Layout::Rect& bounds);
    void NotifyLayout();

    Direction dir_ = Direction::Horizontal;
    float handle_size_ = 6.0f;
    std::vector<Panel> panels_;
    std::vector<float> sizes_;   // resolved fractions per panel
    std::vector<Layout::Rect> handle_rects_;
    int drag_handle_ = -1;
    float drag_start_ = 0.0f;
    float drag_start_frac_ = 0.0f;
    std::function<void(const std::vector<float>&)> on_layout_;
    bool rendered_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_RESIZABLE_H
