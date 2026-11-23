#ifndef XER_UI_WIDGETS_CARD_H
#define XER_UI_WIDGETS_CARD_H

#include <functional>
#include <memory>
#include <string>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Card — shadcn-style surface container with views::BoxLayout-inspired
// regions: an optional media band on top, a header (title/description
// text or a widget slot), a content area and a footer slot. Children
// added via AddChild fill the content area — full back-compat with the
// original padded-box behavior.
class Card final : public Widget {
public:
    // Padding preset (shadcn card densities).
    enum class Density {
        Comfortable,  // spacing_large padding (24 DIP default theme)
        Compact       // spacing_medium padding — dense panels
    };

    Card();

    // Text header drawn when no header widget is set (shadcn CardTitle /
    // CardDescription). Both optional.
    void SetTitle(std::string title);
    const std::string& GetTitle() const { return title_; }
    void SetDescription(std::string description);
    const std::string& GetDescription() const { return description_; }

    // Region slots: header pinned under the media band, footer pinned to
    // the card bottom, content filling the space between (children added
    // via AddChild share that same content area). Passing nullptr clears
    // the slot.
    void SetHeader(std::shared_ptr<Widget> header);
    void SetContent(std::shared_ptr<Widget> content);
    void SetFooter(std::shared_ptr<Widget> footer);
    std::shared_ptr<Widget> GetHeader() const { return header_; }
    std::shared_ptr<Widget> GetContent() const { return content_; }
    std::shared_ptr<Widget> GetFooter() const { return footer_; }

    // Media/thumbnail band drawn edge-to-edge at the card top (shadcn
    // card image). The renderer decodes + caches the path; an empty path
    // clears the band. Height defaults to 140 DIP once an image is set.
    void SetMediaImage(std::string path);
    const std::string& GetMediaImage() const { return media_image_; }
    void SetMediaHeight(float height);
    float GetMediaHeight() const { return media_height_; }

    void SetDensity(Density density);
    Density GetDensity() const { return density_; }

    // Selection ring (Qt item selection / shadcn ring): accent-colored
    // stroke inside the border.
    void SetSelected(bool selected);
    bool IsSelected() const { return selected_; }

    // Whole-card action: the card gains a hover wash + press depth and
    // Click fires the callback (QAbstractButton-style card). Makes the
    // card focusable; Enter/Space activate it too.
    void SetClickable(bool clickable);
    bool IsClickable() const { return clickable_; }
    void SetOnClick(std::function<void()> callback);

    // Collapse behavior (views collapsible sections): shows a chevron in
    // the header corner; toggling animates the content area height to
    // zero while header/media/footer stay put. SetCollapsed also works
    // programmatically on non-collapsible cards.
    void SetCollapsible(bool collapsible);
    bool IsCollapsible() const { return collapsible_; }
    void SetCollapsed(bool collapsed);
    bool IsCollapsed() const { return collapsed_; }
    // 1.0 = fully expanded, 0.0 = fully collapsed (animated value).
    float CollapseProgress() const { return collapse_t_.Value(); }
    void SetOnCollapsedChanged(std::function<void(bool)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    float PaddingFor(const Theme::Tokens& tokens) const;
    float HeaderHeight() const;      // header slot height, 0 when absent
    float FooterHeight() const;      // footer slot height, 0 when absent
    float ContentHeight(float avail) const;

    std::string title_;
    std::string description_;
    std::shared_ptr<Widget> header_;
    std::shared_ptr<Widget> content_;
    std::shared_ptr<Widget> footer_;
    std::string media_image_;
    float media_height_ = 140.0f;
    Density density_ = Density::Comfortable;
    bool selected_ = false;
    bool clickable_ = false;
    bool collapsible_ = false;
    bool collapsed_ = false;
    std::function<void()> on_click_;
    std::function<void(bool)> on_collapsed_changed_;

    Layout::Rect chevron_rect_{};
    // Motion state: press depth, hover wash, content-height collapse.
    Motion::Spring press_;
    Motion::Tween hover_t_;
    Motion::Tween collapse_t_;
    bool held_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CARD_H
