#ifndef XER_UI_WIDGETS_SKELETON_H
#define XER_UI_WIDGETS_SKELETON_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style skeleton: a shimmering placeholder block used while content
// loads. `variant` selects a text-line or block shape.
class Skeleton final : public Widget {
public:
    enum class Variant { Block, Text };

    Skeleton(float width = 0.0f, float height = 0.0f, Variant variant = Variant::Block);

    void SetSize(float width, float height);
    void SetVariant(Variant variant);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    float width_ = 0.0f;
    float height_ = 0.0f;
    Variant variant_ = Variant::Block;
    // Shimmer phase in seconds; advanced by Tick so the sweep is frame-synced.
    float shimmer_t_ = 0.0f;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SKELETON_H
