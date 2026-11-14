#include "calendar.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Calendar> Calendar::Create() {
    return std::shared_ptr<Calendar>(new Calendar());
}

Calendar::Calendar() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    today_ = {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
    view_ = today_;
    selected_ = today_;
}

int Calendar::DaysInMonth(int y, int m) const {
    static const int dim[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
    if (m == 2) {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        return leap ? 29 : 28;
    }
    return dim[m - 1];
}

int Calendar::FirstWeekday(int y, int m) const {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = 1;
#ifdef _WIN32
    mktime(&tm);
#else
    mktime(&tm);
#endif
    return tm.tm_wday;  // 0 = Sunday
}

bool Calendar::DateEnabled(int y, int m, int d) const {
    const long v = y * 10000L + m * 100L + d;
    if (has_min_) {
        const long lo = min_date_.year * 10000L +
                        min_date_.month * 100L + min_date_.day;
        if (v < lo) return false;
    }
    if (has_max_) {
        const long hi = max_date_.year * 10000L +
                        max_date_.month * 100L + max_date_.day;
        if (v > hi) return false;
    }
    return true;
}

void Calendar::SetDate(Date date) {
    selected_ = date;
    view_ = {date.year, date.month, date.day};
    Invalidate();
}

void Calendar::GoToMonth(int year, int month) {
    month = std::clamp(month, 1, 12);
    year += (month - 1) / 12;  // normalize overflow
    month = (month - 1) % 12 + 1;
    view_.year = year;
    view_.month = month;
    Invalidate();
}

void Calendar::SetMinDate(Date date) {
    min_date_ = date;
    has_min_ = true;
    Invalidate();
}

void Calendar::SetMaxDate(Date date) {
    max_date_ = date;
    has_max_ = true;
    Invalidate();
}

void Calendar::SetOnSelect(std::function<void(Date)> cb) {
    on_select_ = std::move(cb);
}

void Calendar::NavigateMonth(int delta) {
    int m = view_.month + delta;
    int y = view_.year;
    while (m < 1) { m += 12; --y; }
    while (m > 12) { m -= 12; ++y; }
    view_.month = m;
    view_.year = y;
    Invalidate();
}

Layout::Rect Calendar::CellRect(int idx,
                                const Layout::Rect& b) const {
    // idx 0..41: 6 rows x 7 cols covering leading blanks + month days.
    const float header_h = 30.0f;
    const float grid_top = b.y + header_h;
    const float grid_h = b.height - header_h;
    const float cell_w = b.width / 7.0f;
    const float cell_h = grid_h / 6.0f;
    const int row = idx / 7;
    const int col = idx % 7;
    return {b.x + col * cell_w, grid_top + row * cell_h, cell_w, cell_h};
}

void Calendar::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    prev_rect_ = {bounds.x + 6.0f, bounds.y + 4.0f, 22.0f, 22.0f};
    next_rect_ = {bounds.x + bounds.width - 28.0f, bounds.y + 4.0f,
                  22.0f, 22.0f};
}

void Calendar::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    renderer.FillRect(b, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_medium);

    // Header: ‹ Month Year ›
    static const char* kMonths[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    const std::string title =
        std::string(kMonths[view_.month - 1]) + " " +
        std::to_string(view_.year);
    const Rendering::TextStyle ts{tokens.foreground, tokens.font_body, 600};
    Layout::Rect tr{b.x, b.y + 4.0f, b.width, 26.0f};
    renderer.DrawText(title, tr, ts);
    // Nav arrows.
    const float ax = 14.0f, ay = b.y + 9.0f;
    renderer.DrawLine({prev_rect_.x + 14.0f, prev_rect_.y + 11.0f},
                      {prev_rect_.x + 8.0f, prev_rect_.y + 6.0f},
                      tokens.foreground, 1.6f);
    renderer.DrawLine({prev_rect_.x + 8.0f, prev_rect_.y + 6.0f},
                      {prev_rect_.x + 8.0f, prev_rect_.y + 16.0f},
                      tokens.foreground, 1.6f);
    renderer.DrawLine({next_rect_.x + 8.0f, next_rect_.y + 11.0f},
                      {next_rect_.x + 14.0f, next_rect_.y + 6.0f},
                      tokens.foreground, 1.6f);
    renderer.DrawLine({next_rect_.x + 14.0f, next_rect_.y + 6.0f},
                      {next_rect_.x + 14.0f, next_rect_.y + 16.0f},
                      tokens.foreground, 1.6f);
    (void)ax; (void)ay;

    // Weekday labels.
    static const char* kDays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    const Rendering::TextStyle ws{tokens.muted, tokens.font_small, 500};
    for (int i = 0; i < 7; ++i) {
        Layout::Rect r{b.x + i * (b.width / 7.0f), b.y + 30.0f,
                       b.width / 7.0f, 18.0f};
        renderer.DrawText(kDays[i], r, ws);
    }

    // Day cells: leading blanks then days.
    const int first_wd = FirstWeekday(view_.year, view_.month);
    const int dim = DaysInMonth(view_.year, view_.month);
    for (int idx = 0; idx < 42; ++idx) {
        const int day_num = idx - first_wd + 1;
        if (day_num < 1 || day_num > dim) continue;
        const auto cell = CellRect(idx + 7, b);  // +7 skips weekday row
        const bool sel = selected_.year == view_.year &&
                         selected_.month == view_.month &&
                         selected_.day == day_num;
        const bool today = today_.year == view_.year &&
                           today_.month == view_.month &&
                           today_.day == day_num;
        const bool enabled = DateEnabled(view_.year, view_.month, day_num);
        const bool hov = hover_cell_ == idx + 7;

        if (sel) {
            renderer.FillRect(cell, tokens.accent, cell.height * 0.5f);
        } else if (hov && enabled) {
            renderer.FillRect(cell, tokens.hover, cell.height * 0.5f);
        }
        const Theme::Color tc =
            sel ? tokens.accent_foreground
                : (!enabled ? tokens.muted : tokens.foreground);
        const Rendering::TextStyle ds{tc, tokens.font_small, 400};
        // Center the day number.
        const float nw = 20.0f;
        Layout::Rect nr{cell.x + (cell.width - nw) * 0.5f,
                        cell.y + (cell.height - 18.0f) * 0.5f, nw, 18.0f};
        renderer.DrawText(std::to_string(day_num), nr, ds);
        if (today && !sel) {
            // Today ring.
            renderer.StrokeRect(
                {cell.x + cell.width * 0.5f - 10.0f,
                 cell.y + cell.height * 0.5f - 9.0f, 20.0f, 18.0f},
                tokens.accent, 1.2f, 9.0f);
        }
    }
}

bool Calendar::HandleEvent(const Event& event) {
    const auto& p = event.position;
    const auto b = GetBounds();

    if (event.type == Event::Type::Click) {
        if (p.x >= prev_rect_.x && p.x < prev_rect_.x + prev_rect_.width &&
            p.y >= prev_rect_.y && p.y < prev_rect_.y + prev_rect_.height) {
            NavigateMonth(-1);
            return true;
        }
        if (p.x >= next_rect_.x && p.x < next_rect_.x + next_rect_.width &&
            p.y >= next_rect_.y && p.y < next_rect_.y + next_rect_.height) {
            NavigateMonth(1);
            return true;
        }
        // Day cells.
        const int first_wd = FirstWeekday(view_.year, view_.month);
        const int dim = DaysInMonth(view_.year, view_.month);
        for (int idx = 0; idx < 42; ++idx) {
            const int day_num = idx - first_wd + 1;
            if (day_num < 1 || day_num > dim) continue;
            const auto cell = CellRect(idx + 7, b);
            if (p.x >= cell.x && p.x < cell.x + cell.width &&
                p.y >= cell.y && p.y < cell.y + cell.height) {
                if (DateEnabled(view_.year, view_.month, day_num)) {
                    selected_ = {view_.year, view_.month, day_num};
                    if (on_select_) on_select_(selected_);
                    Invalidate();
                }
                return true;
            }
        }
        return false;
    }
    if (event.type == Event::Type::MouseMove) {
        const int first_wd = FirstWeekday(view_.year, view_.month);
        const int dim = DaysInMonth(view_.year, view_.month);
        int hov = -1;
        for (int idx = 0; idx < 42; ++idx) {
            const int day_num = idx - first_wd + 1;
            if (day_num < 1 || day_num > dim) continue;
            const auto cell = CellRect(idx + 7, b);
            if (p.x >= cell.x && p.x < cell.x + cell.width &&
                p.y >= cell.y && p.y < cell.y + cell.height) {
                hov = idx + 7;
                break;
            }
        }
        if (hov != hover_cell_) {
            hover_cell_ = hov;
            Invalidate();
        }
        return false;
    }
    return false;
}

Layout::Size Calendar::GetMinSize() const {
    return {200.0f, 240.0f};
}

Layout::Size Calendar::GetPreferredSize() const {
    return {280.0f, 300.0f};
}

Layout::Size Calendar::GetMaxSize() const {
    return {400.0f, 400.0f};
}

} // namespace XER::UI::Widgets
