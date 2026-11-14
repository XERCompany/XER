#ifndef XER_UI_WIDGETS_FORM_H
#define XER_UI_WIDGETS_FORM_H

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Form — vertical (or multi-column) container for Field/Item controls.
// Children are distributed into `columns` equal-width cells with `gap`
// spacing, row-major order.
class Form final : public Widget {
public:
    Form(int columns = 1, float gap = -1.0f);

    void SetColumns(int columns);
    int GetColumns() const { return columns_; }
    void SetGap(float gap);
    float GetGap() const { return gap_; }

    void Render(Rendering::Renderer&, const Theme::Tokens&) override {}
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    float Gap(const Theme::Tokens& tokens) const;

    int columns_ = 1;
    float gap_ = -1.0f;  // <0 → theme spacing_medium
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_FORM_H
