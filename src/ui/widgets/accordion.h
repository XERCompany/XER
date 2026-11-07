#ifndef XER_UI_WIDGETS_ACCORDION_H
#define XER_UI_WIDGETS_ACCORDION_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style accordion: stacked collapsible sections. Each item has a
// header (always visible) and an optional content widget shown when open.
// `single_open` matches shadcn type="single"; otherwise multiple may open.
class Accordion final : public Widget {
public:
    struct Item {
        std::string title;
        std::shared_ptr<Widget> content;
        bool open = false;
    };

    void SetItems(std::vector<Item> items, bool single_open = true);
    void SetItemOpen(std::size_t index, bool open);
    bool IsItemOpen(std::size_t index) const;
    void SetOnToggled(std::function<void(std::size_t, bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    std::size_t HitHeader(const Layout::Point& position) const;
    void RebuildChildren();

    std::vector<Item> items_;
    std::vector<Layout::Rect> header_rects_;
    std::vector<float> content_heights_;
    bool single_open_ = true;
    int hovered_header_ = -1;
    std::function<void(std::size_t, bool)> on_toggled_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ACCORDION_H
