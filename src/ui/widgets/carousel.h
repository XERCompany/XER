#ifndef XER_UI_WIDGETS_CAROUSEL_H
#define XER_UI_WIDGETS_CAROUSEL_H

#include <functional>
#include <memory>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Carousel — horizontal slide deck of child widgets with prev/next
// arrows, dot indicators, keyboard navigation and swipe support.
class Carousel final : public Widget {
public:
    static std::shared_ptr<Carousel> Create();

    void AddSlide(std::shared_ptr<Widget> slide);
    void ClearSlides();
    void SetIndex(int index);       // animated scroll to slide
    int GetIndex() const { return index_; }
    int GetCount() const { return static_cast<int>(slides_.size()); }
    void SetLoop(bool loop);
    void SetShowArrows(bool show);
    void SetShowDots(bool show);
    void SetOnIndexChanged(std::function<void(int)> cb);
    void Next();
    void Prev();

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Carousel();
    void GoTo(int index, bool animate);
    Layout::Rect ArrowRect(bool next, const Layout::Rect& b) const;
    Layout::Rect DotsRect(const Layout::Rect& b) const;

    std::vector<std::shared_ptr<Widget>> slides_;
    int index_ = 0;
    float scroll_ = 0.0f;      // animated pixel offset
    float target_scroll_ = 0.0f;
    bool loop_ = true;
    bool show_arrows_ = true;
    bool show_dots_ = true;
    bool dragging_ = false;
    float drag_start_x_ = 0.0f;
    float drag_start_scroll_ = 0.0f;
    Motion::Tween scroll_t_;
    Layout::Rect prev_rect_{};
    Layout::Rect next_rect_{};
    Layout::Rect dots_rect_{};
    std::function<void(int)> on_index_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CAROUSEL_H
