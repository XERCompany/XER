#include "chart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Chart> Chart::Create(Type type) {
    return std::shared_ptr<Chart>(new Chart(type));
}

Chart::Chart(Type type) : type_(type) {}

void Chart::SetType(Type type) {
    type_ = type;
    Invalidate();
}

void Chart::SetSeries(std::vector<Series> series) {
    series_ = std::move(series);
    ComputeRange();
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
}

void Chart::SetOnPointClick(
    std::function<void(int series, int index)> cb) {
    on_point_click_ = std::move(cb);
}

void Chart::ComputeRange() const {
    min_v_ = 0.0f;
    max_v_ = 1.0f;
    bool first = true;
    for (const auto& s : series_) {
        for (float v : s.values) {
            if (first) {
                min_v_ = max_v_ = v;
                first = false;
            } else {
                min_v_ = std::min(min_v_, v);
                max_v_ = std::max(max_v_, v);
            }
        }
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
    const float bottom = show_legend_ && !series_.empty() ? 44.0f : 24.0f;
    const float top = title_.empty() ? 12.0f : 34.0f;
    const float right = 12.0f;
    return {b.x + left, b.y + top, b.width - left - right,
            b.height - top - bottom};
}

void Chart::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (show_legend_ && !series_.empty()) {
        legend_rect_ = {bounds.x + 12.0f,
                        bounds.y + bounds.height - 36.0f,
                        bounds.width - 24.0f, 24.0f};
    }
}

void Chart::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f || series_.empty()) return;

    // Title.
    if (!title_.empty()) {
        const Rendering::TextStyle ts{tokens.foreground,
                                      tokens.font_body, 600};
        Layout::Rect tr{b.x + 12.0f, b.y + 6.0f, b.width - 24.0f, 24.0f};
        renderer.DrawText(title_, tr, ts);
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
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", v);
            Layout::Rect lr{b.x + 4.0f, fy - 8.0f, 38.0f, 16.0f};
            renderer.DrawText(buf, lr, gs);
        }
    }

    // Determine point count (max across series).
    size_t n = 0;
    for (const auto& s : series_) n = std::max(n, s.values.size());
    if (n == 0) return;

    const float dx = n > 1 ? plot.width / static_cast<float>(n - 1) : 0.0f;

    auto y_of = [&](float v) {
        return plot.y + plot.height -
               (v - min_v_) / (max_v_ - min_v_) * plot.height;
    };

    // Draw series.
    for (size_t si = 0; si < series_.size(); ++si) {
        const auto& s = series_[si];
        const auto color = SeriesColor(si, tokens);
        const size_t m = std::min(n, s.values.size());
        if (m == 0) continue;

        if (type_ == Type::Bar) {
            const float group_w = dx > 0 ? dx * 0.8f : plot.width * 0.8f;
            const float bar_w =
                group_w / static_cast<float>(series_.size());
            for (size_t i = 0; i < m; ++i) {
                const float x = plot.x + i * dx +
                                (si - series_.size() * 0.5f + 0.5f) * bar_w;
                const float y = y_of(s.values[i]);
                const float base = y_of(0.0f);
                const float top = std::min(y, base);
                const float h = std::abs(base - y);
                renderer.FillRect({x, top, std::max(1.0f, bar_w - 1.0f), h},
                                  color, 2.0f);
            }
        } else {
            // Line / Area — polyline through points.
            std::vector<Layout::Point> pts;
            pts.reserve(m);
            for (size_t i = 0; i < m; ++i) {
                pts.push_back({plot.x + i * dx, y_of(s.values[i])});
            }
            if (type_ == Type::Area && pts.size() > 1) {
                // Fill under the line (approximated with horizontal strips).
                const float base = y_of(std::min(0.0f, min_v_));
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    const float x0 = pts[i].x, x1 = pts[i + 1].x;
                    const float y0 = std::min(pts[i].y, pts[i + 1].y);
                    const float y1 = std::max(pts[i].y, pts[i + 1].y);
                    renderer.FillRect({x0, y0, std::max(1.0f, x1 - x0),
                                       std::max(0.0f, base - y0)},
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
}

bool Chart::HandleEvent(const Event& event) {
    if (event.type != Event::Type::Click || series_.empty()) return false;
    const auto& p = event.position;
    const auto plot = PlotRect(GetBounds());
    if (p.x < plot.x || p.x >= plot.x + plot.width ||
        p.y < plot.y || p.y >= plot.y + plot.height) {
        return false;
    }
    size_t n = 0;
    for (const auto& s : series_) n = std::max(n, s.values.size());
    if (n == 0) return false;
    const float dx = n > 1 ? plot.width / static_cast<float>(n - 1) : 0.0f;
    const int idx = dx > 0 ? static_cast<int>(std::lround(
                                 (p.x - plot.x) / dx))
                           : 0;
    if (idx < 0 || idx >= static_cast<int>(n)) return false;
    // Find nearest series at this index.
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
    if (best >= 0 && on_point_click_) on_point_click_(best, idx);
    return best >= 0;
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
