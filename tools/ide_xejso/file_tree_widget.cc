// file_tree_widget.cc — see file_tree_widget.h.
#include "file_tree_widget.h"

#include <algorithm>
#include <cmath>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::IdeXejso {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::MouseButton;

}  // namespace

FileTreeWidget::FileTreeWidget() {
    SetFocusable(false);
    SetClipsChildren(false);
}

void FileTreeWidget::SetOnActivate(
    std::function<void(const std::string&)> callback) {
    on_activate_ = std::move(callback);
}

void FileTreeWidget::SetRoot(const std::string& dir) {
    root_ = dir;
    Refresh();
}

void FileTreeWidget::SetSelected(const std::string& rel_path) {
    if (selected_ == rel_path) return;
    selected_ = rel_path;
    Invalidate();
}

void FileTreeWidget::Refresh() {
    std::vector<std::string> closed(collapsed_.begin(),
                                    collapsed_.end());
    items_ = ListProjectFiles(root_, closed);
    scroll_ = std::clamp(scroll_, 0.0f, MaxScroll());
    Invalidate();
}

int FileTreeWidget::RowAt(Point p) const {
    const Rect b = GetBounds();
    if (!b.Contains(p)) return -1;
    const int row = static_cast<int>(
        (p.y - b.y - 4.0f) / row_h_ + scroll_);
    return row >= 0 && row < static_cast<int>(items_.size()) ? row : -1;
}

float FileTreeWidget::MaxScroll() const {
    const Rect b = GetBounds();
    const float visible =
        row_h_ > 0.0f ? (b.height - 8.0f) / row_h_ : 0.0f;
    return std::max(0.0f, static_cast<float>(items_.size()) - visible);
}

void FileTreeWidget::Render(XER::UI::Rendering::Renderer& renderer,
                            const XER::UI::Theme::Tokens& tokens) {
    const Rect b = GetBounds();
    renderer.FillRect(b, tokens.panel, 0.0f);
    renderer.StrokeRect({b.Right() - 1.0f, b.y, 1.0f, b.height},
                        tokens.border, 0.0f, 0.0f);
    if (items_.empty()) {
        renderer.DrawText(
            root_.empty() ? "no project open" : "(empty)",
            {b.x + 12.0f, b.y + 8.0f, b.width - 16.0f, row_h_},
            {tokens.muted, tokens.font_small, 400});
        return;
    }

    renderer.PushClip(b);
    const int first = static_cast<int>(std::floor(scroll_));
    const float frac = scroll_ - static_cast<float>(first);
    const int visible =
        static_cast<int>(b.height / row_h_) + 2;
    for (int i = 0; i < visible; ++i) {
        const int row = first + i;
        if (row >= static_cast<int>(items_.size())) break;
        const FileItem& item = items_[static_cast<std::size_t>(row)];
        const float y = b.y + 4.0f +
            (static_cast<float>(i) - frac) * row_h_;
        const Rect row_rect{b.x, y, b.width, row_h_};
        const std::string no_slash = item.is_dir
            ? item.rel_path.substr(0, item.rel_path.size() - 1)
            : item.rel_path;
        if (no_slash == selected_ || item.rel_path == selected_) {
            renderer.FillRect(row_rect, tokens.selection, 0.0f);
        } else if (row == hovered_) {
            renderer.FillRect(row_rect, tokens.hover, 0.0f);
        }
        const float indent = 8.0f + item.depth * 14.0f;
        // Disclosure glyph + the row's leaf name.
        const std::string leaf =
            no_slash.substr(no_slash.find_last_of('/') ==
                                    std::string::npos
                                ? 0
                                : no_slash.find_last_of('/') + 1);
        std::string label;
        XER::UI::Theme::Color color = tokens.foreground;
        if (item.is_dir) {
            label = (collapsed_.count(no_slash) ? "> " : "v ") +
                    leaf + "/";
        } else {
            label = "  " + leaf;
            if (leaf == "manifest.toml" ||
                (leaf.size() >= 4 &&
                 leaf.substr(leaf.size() - 4) == ".cc")) {
                color = tokens.accent;
            }
        }
        renderer.DrawText(
            label,
            {b.x + indent, y + (row_h_ - tokens.font_small) * 0.5f,
             b.width - indent - 4.0f, row_h_},
            {color, tokens.font_small, item.is_dir ? 600 : 400});
    }
    renderer.PopClip();
}

void FileTreeWidget::Layout(const Rect& bounds) {
    SetBounds(bounds);
}

bool FileTreeWidget::HandleEvent(const Event& event) {
    switch (event.type) {
        case Event::Type::MouseMove:
            hovered_ = RowAt(event.position);
            Invalidate();
            return false;   // hover is advisory — don't eat the move
        case Event::Type::MouseWheel: {
            if (!GetBounds().Contains(event.position)) return false;
            scroll_ = std::clamp(
                scroll_ - static_cast<float>(event.wheel_delta) /
                              120.0f * 3.0f,
                0.0f, MaxScroll());
            Invalidate();
            return true;
        }
        case Event::Type::Click: {
            if (event.mouse_button != MouseButton::Left) return false;
            const int row = RowAt(event.position);
            if (row < 0) return false;
            const FileItem& item = items_[static_cast<std::size_t>(row)];
            if (item.is_dir) {
                const std::string rel =
                    item.rel_path.substr(0, item.rel_path.size() - 1);
                if (!collapsed_.erase(rel)) collapsed_.insert(rel);
                Refresh();
                return true;
            }
            SetSelected(item.rel_path);
            if (on_activate_) on_activate_(item.rel_path);
            return true;
        }
        default:
            return false;
    }
}

XER::UI::Layout::Size FileTreeWidget::GetMinSize() const {
    return {120.0f, 80.0f};
}
XER::UI::Layout::Size FileTreeWidget::GetPreferredSize() const {
    return {240.0f, 400.0f};
}
XER::UI::Layout::Size FileTreeWidget::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

}  // namespace XER::IdeXejso
