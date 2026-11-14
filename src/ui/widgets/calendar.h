#ifndef XER_UI_WIDGETS_CALENDAR_H
#define XER_UI_WIDGETS_CALENDAR_H

#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Calendar — month-grid date picker. Prev/next month nav, today marker,
// selected-day highlight, optional min/max date clamping.
class Calendar final : public Widget {
public:
    struct Date {
        int year = 1970;
        int month = 1;   // 1..12
        int day = 1;     // 1..31
    };

    static std::shared_ptr<Calendar> Create();

    void SetDate(Date date);          // also navigates to its month
    Date GetDate() const { return selected_; }
    void GoToMonth(int year, int month);
    void SetMinDate(Date date);       // 0/negative disables
    void SetMaxDate(Date date);
    void SetOnSelect(std::function<void(Date)> cb);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Calendar();
    int DaysInMonth(int y, int m) const;
    int FirstWeekday(int y, int m) const;  // 0=Sunday
    bool DateEnabled(int y, int m, int d) const;
    Layout::Rect CellRect(int idx, const Layout::Rect& b) const;
    void NavigateMonth(int delta);

    Date view_{2025, 1, 1};
    Date selected_{};
    Date today_{};
    Date min_date_{};
    Date max_date_{};
    bool has_min_ = false;
    bool has_max_ = false;
    int hover_cell_ = -1;
    Layout::Rect prev_rect_{};
    Layout::Rect next_rect_{};
    std::function<void(Date)> on_select_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CALENDAR_H
