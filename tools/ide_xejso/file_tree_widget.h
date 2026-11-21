// file_tree_widget.h — the IDE's left-pane project browser.
//
// src/ui/widgets has no Tree widget, so this is a small custom-drawn
// list: rows of FileItem from ide_project::ListProjectFiles, dirs
// collapsible via a leading "▾/▸" affordance, mouse-wheel scrolling,
// click-to-select (files fire on_activate, dirs toggle).
#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "ide_project.h"
#include "widgets/widget.h"

namespace XER::IdeXejso {

class FileTreeWidget final : public XER::UI::Widgets::Widget {
public:
    FileTreeWidget();

    // Rebuilds the row model from disk. The collapsed set and the
    // selection (when still present) survive a refresh.
    void SetRoot(const std::string& dir);
    const std::string& Root() const { return root_; }

    // Fired on file-row click (rel_path WITHOUT trailing '/').
    void SetOnActivate(
        std::function<void(const std::string& rel_path)> callback);

    const std::string& Selected() const { return selected_; }
    void SetSelected(const std::string& rel_path);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

private:
    void Refresh();
    int RowAt(XER::UI::Layout::Point p) const;
    float MaxScroll() const;

    std::string root_;
    std::vector<FileItem> items_;
    std::set<std::string> collapsed_;
    std::string selected_;
    int hovered_ = -1;
    float scroll_ = 0.0f;      // first visible row (fractional ok)
    float row_h_ = 22.0f;
    std::function<void(const std::string&)> on_activate_;
};

}  // namespace XER::IdeXejso
