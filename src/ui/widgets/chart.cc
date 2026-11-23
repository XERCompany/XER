#include "chart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

// Bottom margin bookkeeping (DIP): room for X category labels and/or
// the legend strip below the plot.
constexpr float kXLabelBand = 18.0f;
constexpr float kLegendBand = 30.0f;
constexpr float kBottomPad = 8.0f;

} // namespace

std::shared_ptr<Chart> Chart::Create(Type type) {
    return std::shared_ptr<Chart>(new Chart(type));
}

Chart::Chart(Type type) : type_(type) {
    // Rest at fully-animated; SetSeries rewinds when updates animate.
    anim_t_.from = 1.0f;
    anim_t_.to = 1.0f;
}

void Chart::SetType(Type type) {
    type_ = type;
    ComputeRange();
    Invalidate();
}

void Chart::SetSeries(std::vector<Series> series) {
    series_ = std::move(series);
    hover_series_ = -1;
    hover_index_ = -1;
    ComputeRange();
    if (animate_updates_) {
        anim_t_.from = 0.0f;
        anim_t_.to = 0.0f;
        anim_t_.t = 1.0f;
        anim_t_.Set(1.0f, 0.4f);
    }
    Invalidate();
}

void Chart::SetTitle(std::string title) {
    title_ = std::move(title);
    Invalidate();
}

void Chart::SetShowGrid(bool show) {
    show_grid_ = show;
    Invalidate();
}

void Chart::SetShowLegend(bool show) {
    show_legend_ = show;
    Invalidate();
    RequestLayout();
}

void Chart::SetOnPointClick(
    std::function<void(int series, int index)> cb) {
    on_point_click_ = std::move(cb);
}

void Chart::SetStacked(bool stacked) {
    if (stacked_ == stacked) {
        return;
    }
    stacked_ = stacked;
    ComputeRange();
    Invalidate();
}

void Chart::SetXLabels(std::vector<std::string> labels) {
    x_labels_ = std::move(labels);
    Invalidate();
    RequestLayout();
}

void Chart::SetValueFormatter(
    std::function<std::string(float)> formatter) {
    value_formatter_ = std::move(formatter);
    Invalidate();
}

void Chart::SetTooltipEnabled(bool enabled) {
    if (tooltip_enabled_ == enabled) {
        return;
    }
    tooltip_enabled_ = enabled;
    if (!enabled) {
        hover_series_ = -1;
        hover_index_ = -1;
    }
    Invalidate();
}

void Chart::SetYRange(float min, float max) {
    has_y_range_ = true;
    range_min_ = std::min(min, max);
    range_max_ = std::max(min, max);
    ComputeRange();
    Invalidate();
}

void Chart::ClearYRange() {
    has_y_range_ = false;
    ComputeRange();
    Invalidate();
}

void Chart::SetEmptyText(std::string text) {
    empty_text_ = std::move(text);
    Invalidate();
}

void Chart::SetAnimateUpdates(bool animate) {
    animate_updates_ = animate;
    if (!animate) {
        anim_t_.from = anim_t_.to = 1.0f;
        anim_t_.t = 1.0f;
    }
}

void Chart::ComputeRange() const {
    if (has_y_range_) {
        min_v_ = range_min_;
        max_v_ = range_max_;
        if (max_v_ - min_v_ < 1e-6f) {
            max_v_ = min_v_ + 1.0f;
        }
        return;
    }
    min_v_ = 0.0f;
    max_v_ = 1.0f;
    bool first = true;
    auto feed = [&](float v) {
        if (first) {
            min_v_ = max_v_ = v;
            first = false;
        } else {
            min_v_ = std::min(min_v_, v);
            max_v_ = std::max(max_v_, v);
        }
    };
    if (stacked_ && type_ == Type::Bar) {
        // Stacked bars range over per-index cumulative sums, with the
        // positive and negative directions tracked independently.
        const size_t n = PointCount();
        for (size_t i = 0; i < n; ++i) {
            float pos = 0.0f, neg = 0.0f;
            for (const auto& s : series_) {
                if (i >= s.values.size()) continue;
                if (s.values[i] >= 0.0f) {
                    pos += s.values[i];
                } else {
                    neg += s.values[i];
                }
            }
            feed(pos);
            feed(neg);
        }
    } else {
        for (const auto& s : series_) {
            for (float v : s.values) {
                feed(v);
            }
        }
    }
    if (first) {
        min_v_ = 0.0f;
        max_v_ = 1.0f;
        return;
    }
    if (max_v_ - min_v_ < 1e-6f) {
        min_v_ -= 0.5f;
        max_v_ += 0.5f;
    }
    // Pad 5%.
    const float pad = (max_v_ - min_v_) * 0.05f;
    min_v_ -= pad;
    max_v_ += pad;
}

std::string Chart::FormatValue(float v) const {
    if (value_formatter_) {
        return value_formatter_(v);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
    return buf;
}

size_t Chart::PointCount() const {
    size_t n = 0;
    for (const auto& s : series_) {
        n = std::max(n, s.values.size());
    }
    return n;
}

Theme::Color Chart::SeriesColor(size_t i,
                                const Theme::Tokens& tokens) const {
    // Cycle through accent/semantic palette.
    switch (i % 4) {
        case 0: return tokens.accent;
        case 1: return tokens.danger;
        case 2: return tokens.focus;
        default: {
            Theme::Color c = tokens.foreground;
            c.a = 0.6f;
            return c;
        }
    }
}

Layout::Rect Chart::PlotRect(const Layout::Rect& b) const {
    const float left = 44.0f;   // Y labels
    const float bottom =
        (show_legend_ && !series_.empty() ? kLegendBand : 0.0f) +
        (!x_labels_.empty() ? kXLabelBand : 0.0f) + kBottomPad;
    const float top = title_.empty() ? 12.0f : 34.0f;
    const float right = 12.0f;
    return {b.x + left, b.y + top, b.width - left - right,
            b.height - top - bottom};
}

void Chart::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (show_legend_ && !series_.empty()) {
        legend_rect_ = {bounds.x + 12.0f,
                        bounds.y + bounds.height - kLegendBand + 6.0f,
                        bounds.width - 24.0f, 24.0f};
    }
}

bool Chart::HitTestPoint(const Layout::Point& p, int* series,
                         int* index) const {
    const auto plot = PlotRect(GetBounds());
    if (p.x < plot.x || p.x >= plot.x + plot.width ||
        p.y < plot.y || p.y >= plot.y + plot.height) {
        return false;
    }
    const size_t n = PointCount();
    if (n == 0) return false;
    const float dx = n > 1 ? plot.width / static_cast<float>(n - 1) : 0.0f;
    const int idx = dx > 0 ? static_cast<int>(std::lround(
                                 (p.x - plot.x) / dx))
                           : 0;
    if (idx < 0 || idx >= static_cast<int>(n)) return false;
    // Nearest series at this index.
    int best = -1;
    float best_dy = 1e9f;
    for (size_t si = 0; si < series_.size(); ++si) {
        if (idx >= static_cast<int>(series_[si].values.size())) continue;
        const float v = series_[si].values[idx];
        const float y = plot.y + plot.height -
                        (v - min_v_) / (max_v_ - min_v_) * plot.height;
        const float dy = std::abs(y - p.y);
        if (dy < best_dy) {
            best_dy = dy;
            best = static_cast<int>(si);
        }
    }
    if (best < 0) return false;
    *series = best;
    *index = idx;
    return true;
}

void Chart::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    // Title.
    if (!title_.empty()) {
        const Rendering::TextStyle ts{tokens.foreground,
                                      tokens.font_body, 600};
        Layout::Rect tr{b.x + 12.0f, b.y + 6.0f, b.width - 24.0f, 24.0f};
        renderer.DrawText(title_, tr, ts);
    }

    // Empty state: centered muted text instead of a bare widget.
    if (series_.empty() || PointCount() == 0) {
        if (!empty_text_.empty()) {
            const Rendering::TextStyle es{tokens.muted,
                                          tokens.font_body, 400};
            const auto sz = renderer.MeasureText(empty_text_, es);
            Layout::Rect er{b.x + (b.width - sz.width) * 0.5f,
                            b.y + (b.height - sz.height) * 0.5f,
                            sz.width + 4.0f, sz.height + 2.0f};
            renderer.DrawText(empty_text_, er, es);
        }
        return;
    }

    const auto plot = PlotRect(b);
    if (plot.width <= 0.0f || plot.height <= 0.0f) return;

    // Grid: 4 horizontal lines + min/max labels.
    if (show_grid_) {
        const Rendering::TextStyle gs{tokens.muted, tokens.font_small,
                                      400};
        for (int i = 0; i <= 4; ++i) {
            const float fy = plot.y + plot.height * i / 4.0f;
            renderer.DrawLine({plot.x, fy}, {plot.x + plot.width, fy},
                              tokens.border, 1.0f);
            const float v = max_v_ - (max_v_ - min_v_) * i / 4.0f;
            const std::string label = FormatValue(v);
            Layout::Rect lr{b.x + 4.0f, fy - 8.0f, 38.0f, 16.0f};
            renderer.DrawText(label, lr, gs);
        }
    }

    const size_t n = PointCount();
    const float dx = n > 1 ? plot.width / static_cast<float>(n - 1) : 0.0f;

    // Update animation: values grow from the zero baseline.
    const float anim = animate_updates_ ? anim_t_.Value() : 1.0f;
    const float base_y_raw = plot.y + plot.height -
        (0.0f - min_v_) / (max_v_ - min_v_) * plot.height;
    const float base_y = std::clamp(base_y_raw, plot.y,
                                    plot.y + plot.height);
    auto y_of = [&](float v) {
        return plot.y + plot.height -
               (v - min_v_) / (max_v_ - min_v_) * plot.height;
    };
    auto y_anim = [&](float v) {
        return base_y - (base_y - y_of(v)) * anim;
    };

    // Draw series.
    for (size_t si = 0; si < series_.size(); ++si) {
        const auto& s = series_[si];
        const auto color = SeriesColor(si, tokens);
        const size_t m = std::min(n, s.values.size());
        if (m == 0) continue;

        if (type_ == Type::Bar) {
            const float group_w = dx > 0 ? dx * 0.8f : plot.width * 0.8f;
            if (stacked_) {
                // Stacked: each index accumulates positive and negative
                // contributions independently (QStackedBarSeries).
                const float bar_w = std::max(2.0f, group_w);
                for (size_t i = 0; i < m; ++i) {
                    const float v = s.values[i];
                    float cum = 0.0f;
                    for (size_t sj = 0; sj < si; ++sj) {
                        if (i >= series_[sj].values.size()) continue;
                        const float pv = series_[sj].values[i];
                        if ((pv >= 0.0f) == (v >= 0.0f)) cum += pv;
                    }
                    const float y0 = y_anim(cum);
                    const float y1 = y_anim(cum + v);
                    const float x = plot.x + i * dx - bar_w * 0.5f;
                    renderer.FillRect(
                        {x, std::min(y0, y1), bar_w,
                         std::abs(y1 - y0)},
                        color, 2.0f);
                }
            } else {
                const float bar_w =
                    group_w / static_cast<float>(series_.size());
                for (size_t i = 0; i < m; ++i) {
                    const float x = plot.x + i * dx +
                                    (si - series_.size() * 0.5f + 0.5f) * bar_w;
                    const float y = y_anim(s.values[i]);
                    const float top = std::min(y, base_y);
                    const float h = std::abs(base_y - y);
                    renderer.FillRect({x, top, std::max(1.0f, bar_w - 1.0f), h},
                                      color, 2.0f);
                }
            }
        } else {
            // Line / Area — polyline through points.
            std::vector<Layout::Point> pts;
            pts.reserve(m);
            for (size_t i = 0; i < m; ++i) {
                pts.push_back({plot.x + i * dx, y_anim(s.values[i])});
            }
            if (type_ == Type::Area && pts.size() > 1) {
                // Fill under the line (approximated with horizontal strips).
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    const float x0 = pts[i].x, x1 = pts[i + 1].x;
                    const float y0 = std::min(pts[i].y, pts[i + 1].y);
                    renderer.FillRect({x0, y0, std::max(1.0f, x1 - x0),
                                       std::max(0.0f, base_y - y0)},
                                      color, 0.0f);
                }
            }
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                renderer.DrawLine(pts[i], pts[i + 1], color, 2.0f);
            }
            // Dots at each point.
            for (const auto& pt : pts) {
                renderer.FillRect({pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f},
                                  color, 2.0f);
            }
        }
    }

    // X category labels, thinned to roughly one per ~56 DIP.
    if (!x_labels_.empty()) {
        const Rendering::TextStyle xs{tokens.muted, tokens.font_small,
                                      400};
        const size_t count = x_labels_.size();
        const size_t step = std::max<size_t>(
            1, static_cast<size_t>(std::ceil(
                   count * 56.0f / std::max(1.0f, plot.width))));
        for (size_t i = 0; i < count; i += step) {
            const float lx = count > 1
                ? plot.x + plot.width * static_cast<float>(i) /
                      static_cast<float>(count - 1)
                : plot.x + plot.width * 0.5f;
            Layout::Rect tr{lx - 28.0f, plot.y + plot.height + 4.0f,
                            56.0f, 14.0f};
            renderer.DrawText(x_labels_[i], tr, xs);
        }
    }

    // Legend.
    if (show_legend_ && !series_.empty()) {
        float lx = legend_rect_.x;
        const Rendering::TextStyle ls{tokens.foreground,
                                      tokens.font_small, 400};
        for (size_t i = 0; i < series_.size(); ++i) {
            const auto color = SeriesColor(i, tokens);
            renderer.FillRect({lx, legend_rect_.y + 6.0f, 10.0f, 10.0f},
                              color, 2.0f);
            Layout::Rect tr{lx + 14.0f, legend_rect_.y, 140.0f, 22.0f};
            renderer.DrawText(series_[i].label, tr, ls);
            lx += 14.0f + 140.0f;
            if (lx > legend_rect_.x + legend_rect_.width - 60.0f) break;
        }
    }

    // Hover tooltip: "label: value" bubble near the hit point
    // (Recharts Tooltip semantics), clamped inside the widget.
    if (tooltip_enabled_ && hover_series_ >= 0 &&
        hover_series_ < static_cast<int>(series_.size())) {
        const auto& s = series_[static_cast<size_t>(hover_series_)];
        if (hover_index_ >= 0 &&
            hover_index_ < static_cast<int>(s.values.size())) {
            const float v = s.values[static_cast<size_t>(hover_index_)];
            const std::string text = s.label.empty()
                ? FormatValue(v)
                : s.label + ": " + FormatValue(v);
            const Rendering::TextStyle ts{tokens.foreground,
                                          tokens.font_small, 400};
            const auto sz = renderer.MeasureText(text, ts);
            const float bw = sz.width + 16.0f;
            const float bh = sz.height + 10.0f;
            const float px = plot.x + hover_index_ * dx;
            const float py = y_of(v);
            float tx = px - bw * 0.5f;
            float ty = py - bh - 10.0f;
            tx = std::clamp(tx, b.x + 2.0f, b.x + b.width - bw - 2.0f);
            ty = std::clamp(ty, b.y + 2.0f, b.y + b.height - bh - 2.0f);
            const Layout::Rect bubble{tx, ty, bw, bh};
            renderer.FillRect(bubble, tokens.surface,
                              tokens.radius_medium);
            renderer.StrokeRect(bubble, tokens.border, 1.0f,
                                tokens.radius_medium);
            renderer.DrawText(
                text,
                Layout::Rect{tx + 8.0f, ty + 5.0f, sz.width + 2.0f,
                             sz.height + 2.0f},
                ts);
            // Hit marker on the point itself.
            const auto c = SeriesColor(
                static_cast<size_t>(hover_series_), tokens);
            renderer.FillRect({px - 3.0f, py - 3.0f, 6.0f, 6.0f}, c,
                              3.0f);
        }
    }
}

bool Chart::HandleEvent(const Event& event) {
    if (series_.empty()) return false;
    switch (event.type) {
        case Event::Type::MouseMove: {
            if (!tooltip_enabled_) return false;
            int si = -1, idx = -1;
            const bool hit = HitTestPoint(event.position, &si, &idx);
            const int new_si = hit ? si : -1;
            const int new_idx = hit ? idx : -1;
            if (new_si != hover_series_ || new_idx != hover_index_) {
                hover_series_ = new_si;
                hover_index_ = new_idx;
                Invalidate();
            }
            return false;
        }
        case Event::Type::Click: {
            int si = -1, idx = -1;
            if (!HitTestPoint(event.position, &si, &idx)) {
                return false;
            }
            if (on_point_click_) on_point_click_(si, idx);
            return true;
        }
        default:
            return false;
    }
}

void Chart::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    if (anim_t_.Active()) {
        anim_t_.Update(static_cast<float>(dt_seconds));
        Invalidate();
    }
}

Layout::Size Chart::GetMinSize() const {
    return {160.0f, 100.0f};
}

Layout::Size Chart::GetPreferredSize() const {
    return {480.0f, 280.0f};
}

Layout::Size Chart::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
