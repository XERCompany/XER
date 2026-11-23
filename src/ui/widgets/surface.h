#ifndef XER_UI_WIDGETS_SURFACE_H
#define XER_UI_WIDGETS_SURFACE_H

#include <functional>
#include <memory>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Generic custom-paint widget: hosts arbitrary app rendering (video
// monitors, timelines, meters, node previews) inside the widget tree —
// the missing primitive between the themed widgets and raw Renderer
// access. The app installs a paint callback invoked from Render() with
// the renderer and the content rect, plus an optional event callback
// receiving raw Widget events.
//
// Geometry contract: SetContentSize() declares the intrinsic content
// size (video frame, canvas resolution). ContentRect() returns the rect
// the paint callback should draw into — with hasScaledContents()==false
// (default) the content is letterboxed preserving aspect; with
// SetScaledContents(true) it stretches to the full bounds
// (QLabel::setScaledContents semantics). With no content size set,
// ContentRect() == GetBounds().
class Surface final : public Widget {
public:
    // Paints the surface contents. `content` is ContentRect() — draw the
    // app's pixels clipped to it; the renderer works in client
    // coordinates and `tokens` is the theme palette passed to Render().
    // Called from Render(), inside a PushClip(bounds).
    using PaintCallback =
        std::function<void(Rendering::Renderer&, const Layout::Rect&,
                           const Theme::Tokens&)>;
    // Raw widget events. Return true to consume. Installing one makes
    // the surface focusable automatically (Clear by passing nullptr).
    using EventCallback = std::function<bool(const Event&)>;
    // Dynamic preferred size, e.g. follow media dimensions. Its return
    // value wins over SetPreferredSize() when it is non-zero.
    using SizeCallback = std::function<Layout::Size()>;

    Surface();

    void SetPaintCallback(PaintCallback callback);
    void SetEventCallback(EventCallback callback);
    void SetPreferredSizeCallback(SizeCallback callback);

    void SetMinSize(Layout::Size size);
    void SetPreferredSize(Layout::Size size);
    void SetMaxSize(Layout::Size size);

    // Intrinsic content size for aspect-preserving letterboxing.
    // {0,0} disables letterboxing (ContentRect() == GetBounds()).
    void SetContentSize(Layout::Size size);
    Layout::Size ContentSize() const { return content_size_; }
    Layout::Rect ContentRect() const;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    PaintCallback paint_callback_;
    EventCallback event_callback_;
    SizeCallback preferred_size_callback_;
    Layout::Size min_size_{0.0f, 0.0f};
    Layout::Size preferred_size_{160.0f, 120.0f};
    Layout::Size max_size_{100000.0f, 100000.0f};
    Layout::Size content_size_{0.0f, 0.0f};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SURFACE_H
