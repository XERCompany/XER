#ifndef XER_UI_WIDGETS_LABEL_H
#define XER_UI_WIDGETS_LABEL_H

#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Label final : public Widget {
public:
    explicit Label(std::string text = {}, bool muted = false);

    void SetText(std::string text);
    const std::string& GetText() const;
    std::string GetDebugText() const override { return text_; }
    void SetMuted(bool muted);
    bool IsMuted() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
    // views::Label contract: true when the display text does not fit the
    // current bounds and Render() draws it ellipsized — callers escalate
    // to a tooltip. Resolved through the same measure chain as
    // GetPreferredSize (target → cache → estimate).
    bool IsDisplayTextClipped() const;

private:
    // Measures `text` in this label's own style. Order: the installed
    // text-measure target (see SetTextMeasureTarget in widget.h) → the
    // Render()-populated cache (only valid for text_) → the legacy
    // char-width estimate, so a Label is honest wherever a renderer has
    // been seen and never lies elsewhere either.
    Layout::Size Measure(std::string_view text) const;
    Rendering::TextStyle MeasureStyle() const;

    std::string text_;
    bool muted_ = false;
    // Measured size of text_ captured by the most recent Render — the
    // no-host path that keeps GetPreferredSize honest in tests/headless
    // layouts once the widget has painted once.
    mutable bool measured_valid_ = false;
    mutable Layout::Size measured_{};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_LABEL_H
