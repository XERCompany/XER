#ifndef XER_UI_WIDGETS_CHART_H
#define XER_UI_WIDGETS_CHART_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion/motion.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Chart — lightweight data visualization: line, area, or bar series
// with auto-scaled Y axis, grid lines, and an optional legend/labels.
// Multi-series is first-class: bars can group or stack (Qt
// QStackedBarSeries semantics), every series auto-derives a legend
// entry from its label.
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

    // Qt QStackedBarSeries: multi-series bars stack per category index
    // (positives and negatives accumulate independently). Ignored for
    // Line/Area.
    void SetStacked(bool stacked);
    bool IsStacked() const { return stacked_; }

    // Category labels under the X axis (Recharts XAxis dataKey /
    // QBarCategoryAxis). Thinned automatically when they don't fit.
    void SetXLabels(std::vector<std::string> labels);
    const std::vector<std::string>& GetXLabels() const { return x_labels_; }

    // Formats grid tick + tooltip values (Recharts tickFormatter).
    // Default prints "%.4g".
    void SetValueFormatter(
        std::function<std::string(float)> formatter);

    // Hover tooltip: shows the nearest point's series label + formatted
    // value in a bubble (renderer hit-test). Off by default to keep the
    // resting visual unchanged.
    void SetTooltipEnabled(bool enabled);
    bool IsTooltipEnabled() const { return tooltip_enabled_; }

    // Fixed Y range (QValueAxis::setMin/setMax). Unset → auto-scale from
    // data with 5% padding.
    void SetYRange(float min, float max);
    void ClearYRange();
    bool HasYRange() const { return has_y_range_; }

    // Empty-state text drawn centered when the chart has no data.
    void SetEmptyText(std::string text);
    const std::string& GetEmptyText() const { return empty_text_; }

    // Animate data updates: values grow in from the baseline when
    // SetSeries runs (short ease-out tween, honors ReducedMotion).
    void SetAnimateUpdates(bool animate);
    bool GetAnimateUpdates() const { return animate_updates_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Chart(Type type);
    void ComputeRange() const;
    Layout::Rect PlotRect(const Layout::Rect& b) const;
    Theme::Color SeriesColor(size_t i, const Theme::Tokens& tokens) const;
    std::string FormatValue(float v) const;
    // Nearest (series, index) to `p`, or (-1,-1) outside the plot.
    bool HitTestPoint(const Layout::Point& p, int* series,
                      int* index) const;
    size_t PointCount() const;

    Type type_ = Type::Line;
    std::vector<Series> series_;
    std::string title_;
    bool show_grid_ = true;
    bool show_legend_ = true;
    bool stacked_ = false;
    std::vector<std::string> x_labels_;
    std::function<std::string(float)> value_formatter_;
    bool tooltip_enabled_ = false;
    bool has_y_range_ = false;
    float range_min_ = 0.0f;
    float range_max_ = 1.0f;
    std::string empty_text_ = "No data";
    bool animate_updates_ = false;
    Motion::Tween anim_t_;
    mutable float min_v_ = 0.0f;
    mutable float max_v_ = 1.0f;
    int hover_series_ = -1;
    int hover_index_ = -1;
    Layout::Rect legend_rect_{};
    std::function<void(int, int)> on_point_click_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CHART_H
