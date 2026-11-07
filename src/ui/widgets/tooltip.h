#ifndef XER_UI_WIDGETS_TOOLTIP_H
#define XER_UI_WIDGETS_TOOLTIP_H

#include <memory>
#include <string>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style tooltip bubble. Not a normal overlay: WidgetTree renders it
// in a passive layer above everything but never hit-tests it, so it can't
// block input. Position is derived from the anchor widget's bounds.
class Tooltip final : public Widget {
public:
    static std::shared_ptr<Tooltip> Create(std::string text);

    // Client-space rect of the widget the tooltip describes; the bubble is
    // placed below it (flipped above when there's no room).
    void SetAnchor(Layout::Rect anchor);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Tick(double dt_seconds) override;

    // Passive overlay: consumes no input.
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Tooltip(std::string text);

    std::string text_;
    Layout::Rect anchor_{};
    Motion::Tween open_t_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TOOLTIP_H
