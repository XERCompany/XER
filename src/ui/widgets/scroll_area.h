#ifndef XER_UI_WIDGETS_SCROLL_AREA_H
#define XER_UI_WIDGETS_SCROLL_AREA_H

#include <memory>

#include "theme/tokens.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class ScrollArea;

// Vertical scrollbar drawn as the topmost child of a ScrollArea. It owns
// the pointer interaction (thumb drag, track paging) and forwards scroll
// requests back to its owner.
class ScrollBarWidget final : public Widget {
public:
    explicit ScrollBarWidget(ScrollArea* owner);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    ScrollArea* owner_;
    bool hovered_ = false;
};

// Vertical scroll container. Lays out a single content widget taller than
// the viewport, clips descendants to the viewport, and provides a
// shadcn-style floating scrollbar with wheel + drag support.
class ScrollArea final : public Widget {
public:
    explicit ScrollArea(std::shared_ptr<Widget> content);

    void SetContent(std::shared_ptr<Widget> content);
    std::shared_ptr<Widget> GetContent() const { return content_; }

    void ScrollTo(float y);
    float GetScrollY() const { return scroll_y_; }
    float GetScrollRange() const { return scroll_range_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override { return {120.0f, 80.0f}; }
    Layout::Size GetPreferredSize() const override { return {480.0f, 360.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }
    float GetContentHeight() const override { return content_height_; }

private:
    friend class ScrollBarWidget;

    static constexpr float kBarWidth = 10.0f;
    static constexpr float kBarPad = 2.0f;

    void EnsureChildrenAttached();
    void RelayoutContent();
    void UpdateScrollRange();
    float ScrollbarTrackTop() const;
    float ScrollbarTrackHeight() const;
    Layout::Rect ScrollbarThumbRect() const;
    void ScrollFromPointer(float y, bool grab_offset, float grab);
    bool ScrollbarZoneContains(const Layout::Point& position) const;

    std::shared_ptr<Widget> content_;
    std::shared_ptr<ScrollBarWidget> scrollbar_;

    float scroll_y_ = 0.0f;
    float scroll_range_ = 0.0f;
    float content_height_ = 0.0f;
    float viewport_height_ = 0.0f;
    bool scrollbar_dragging_ = false;
    float scrollbar_grab_ = 0.0f;
    bool children_attached_ = false;
    Theme::Tokens last_tokens_{};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SCROLL_AREA_H
