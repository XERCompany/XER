#ifndef XER_UI_WIDGETS_CHART_H
#define XER_UI_WIDGETS_CHART_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Chart — lightweight data visualization: line, area, or bar series
// with auto-scaled Y axis, grid lines, and an optional legend/labels.
class Chart final : public Widget {
public:
    enum class Type { Line, Area, Bar };

    struct Series {
        std::string label;
        std::vector<float> values;
    };

    static std::shared_ptr<Chart> Create(Type type = Type::Line);

    void SetType(Type type);
    void SetSeries(std::vector<Series> series);
    void SetTitle(std::string title);
    void SetShowGrid(bool show);
    void SetShowLegend(bool show);
    void SetOnPointClick(std::function<void(int series, int index)> cb);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Chart(Type type);
    void ComputeRange() const;
    Layout::Rect PlotRect(const Layout::Rect& b) const;
    Theme::Color SeriesColor(size_t i, const Theme::Tokens& tokens) const;

    Type type_ = Type::Line;
    std::vector<Series> series_;
    std::string title_;
    bool show_grid_ = true;
    bool show_legend_ = true;
    mutable float min_v_ = 0.0f;
    mutable float max_v_ = 1.0f;
    int hover_series_ = -1;
    int hover_index_ = -1;
    Layout::Rect legend_rect_{};
    std::function<void(int, int)> on_point_click_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CHART_H
