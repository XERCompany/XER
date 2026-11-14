#include "form.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Form::Form(int columns, float gap)
    : columns_(std::max(1, columns)), gap_(gap) {
    SetFocusable(false);
}

void Form::SetColumns(int columns) {
    columns_ = std::max(1, columns);
    RequestLayout();
    Invalidate();
}

void Form::SetGap(float gap) {
    gap_ = gap;
    RequestLayout();
}

float Form::Gap(const Theme::Tokens& tokens) const {
    return gap_ >= 0.0f ? gap_ : tokens.spacing_medium;
}

void Form::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    if (children.empty()) return;

    const auto& tokens = CachedLayoutTokens();
    const float gap = Gap(tokens);
    const int cols = std::min(columns_, static_cast<int>(children.size()));
    const int rows = static_cast<int>((children.size() + cols - 1) / cols);

    // Row heights = max preferred height per row.
    std::vector<float> row_h(static_cast<std::size_t>(rows), 0.0f);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const int row = static_cast<int>(i) / cols;
        row_h[static_cast<std::size_t>(row)] =
            std::max(row_h[static_cast<std::size_t>(row)],
                     children[i]->GetPreferredSize().height);
    }

    const float cell_w =
        (bounds.width - gap * static_cast<float>(cols - 1)) /
        static_cast<float>(cols);
    float y = bounds.y;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const std::size_t i =
                static_cast<std::size_t>(row) * cols + col;
            if (i >= children.size()) break;
            Layout::Rect cell;
            cell.x = bounds.x + static_cast<float>(col) * (cell_w + gap);
            cell.y = y;
            cell.width = cell_w;
            cell.height = row_h[static_cast<std::size_t>(row)];
            children[i]->Layout(cell, tokens);
        }
        y += row_h[static_cast<std::size_t>(row)] + gap;
    }
}

Layout::Size Form::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Form::GetPreferredSize() const {
    const auto& children = GetChildren();
    if (children.empty()) return {0.0f, 0.0f};
    const int cols = std::min(columns_, static_cast<int>(children.size()));
    const int rows = static_cast<int>((children.size() + cols - 1) / cols);
    float h = 0.0f;
    float max_row_w = 0.0f;
    for (int row = 0; row < rows; ++row) {
        float row_h = 0.0f;
        float row_w = 0.0f;
        int cells = 0;
        for (int col = 0; col < cols; ++col) {
            const std::size_t i =
                static_cast<std::size_t>(row) * cols + col;
            if (i >= children.size()) break;
            const auto s = children[i]->GetPreferredSize();
            row_h = std::max(row_h, s.height);
            row_w += s.width;
            ++cells;
        }
        if (cells > 1) row_w += 8.0f * static_cast<float>(cells - 1);
        h += row_h;
        max_row_w = std::max(max_row_w, row_w);
    }
    h += static_cast<float>(rows - 1) * 8.0f;
    return {max_row_w, h};
}

Layout::Size Form::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
