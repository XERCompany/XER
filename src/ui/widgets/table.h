#ifndef XER_UI_WIDGETS_TABLE_H
#define XER_UI_WIDGETS_TABLE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Table — data grid: header row + string cells, row selection, optional
// column widths, simple vertical scroll for long data sets.
class Table final : public Widget {
public:
    struct Column {
        std::string header;
        float width = 0.0f;  // 0 = equal share
        bool sortable = false;
    };

    static std::shared_ptr<Table> Create();

    void SetColumns(std::vector<Column> columns);
    void SetRows(std::vector<std::vector<std::string>> rows);
    void SetHeader(bool show);
    void SetRowHeight(float px);
    void SetSelectedRow(int index);   // -1 clears
    int GetSelectedRow() const { return selected_; }
    void SetOnRowClick(std::function<void(int)> cb);
    void SetOnCellClick(std::function<void(int, int)> cb);
    void ScrollTo(int row);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit Table();
    int RowAtY(float y) const;
    int ColAtX(float x) const;
    Layout::Rect CellRect(int row, int col, const Layout::Rect& b) const;

    std::vector<Column> columns_;
    std::vector<std::vector<std::string>> rows_;
    bool show_header_ = true;
    float row_height_ = 30.0f;
    int selected_ = -1;
    int hovered_ = -1;
    float scroll_ = 0.0f;
    float max_scroll_ = 0.0f;
    Layout::Rect scrollbar_rect_{};
    bool scrollbar_drag_ = false;
    float scrollbar_drag_y_ = 0.0f;  // px grab offset inside the thumb
    std::function<void(int)> on_row_click_;
    std::function<void(int, int)> on_cell_click_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TABLE_H
