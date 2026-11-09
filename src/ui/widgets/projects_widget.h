#ifndef XER_UI_WIDGETS_PROJECTS_WIDGET_H
#define XER_UI_WIDGETS_PROJECTS_WIDGET_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "theme/theme.h"
#include "widgets/widget.h"

namespace XER::NodeGraph {
class ProjectStore;
}

namespace XER::UI::Widgets {

class Button;
class Label;
class TextInput;

// Projects page: lists NodeGraph projects from the SQLite ProjectStore with
// create / open / rename / delete actions. Designed to live inside a
// ScrollArea — reports its full content height via GetContentHeight().
// The widget owns no store; the editor passes a non-owning pointer and
// receives open/save requests through Hooks.
class ProjectsWidget final : public Widget {
public:
    enum class Mode {
        // Embedded page (editor "Projects" tab): name input, save-as,
        // Open / Rename / Delete row actions.
        kPage,
        // Landing screen shown before the editor: search filter, refresh,
        // Select / Configure / Duplicate / Delete row actions. Owns its
        // ProjectStore instance when no external store is set.
        kBrowser,
    };

    struct Hooks {
        // Load the project's graph into the editor and switch to it.
        std::function<void(std::int64_t project_id)> on_open_project;
        // Open a specific graph inside a project (empty name = auto-pick).
        std::function<void(std::int64_t project_id,
                           const std::string& graph_name)>
            on_open_project_graph;
        // Serialize the current graph into the named project (find-or-create).
        std::function<void(const std::string& project_name)> on_save_current_as;
        // Status/toast forwarding (text, is_error).
        std::function<void(const std::string& text, bool is_error)> on_notify;
    };

    ProjectsWidget(void* parent_hwnd, Hooks hooks = {});
    ~ProjectsWidget() override;

    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }

    // Non-owning store pointer; Refresh() is a no-op until set. In browser
    // mode the widget opens its own store at the default projects.db path
    // when this was never called.
    void SetStore(NodeGraph::ProjectStore* store);
    // Re-reads the project list. Call when the page becomes visible and
    // after any mutation.
    void Refresh();

    float GetContentHeight() const override { return content_height_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override { (void)event; return false; }
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override { return {320.0f, 200.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {640.0f, content_height_ > 0.0f ? content_height_ : 480.0f};
    }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    struct Row {
        std::int64_t id = 0;
        std::string name;
        std::string meta;
        std::shared_ptr<Widget> widget;  // ProjectRowWidget (file-local type)
        float top = 0.0f;
        float height = 0.0f;
    };

    void DetachRows();
    void RebuildRows();
    void PromptCreateProject();
    void PromptName(const std::string& title, const std::string& initial,
                    std::function<void(std::string)> on_submit,
                    std::string confirm_label = "OK",
                    std::string description = {},
                    std::string field_caption = "Name");
    void PromptProjectConfig(std::int64_t id, const std::string& name);
    void CreateNamedProject(const std::string& name);
    void DuplicateProject(std::int64_t id, const std::string& name);
    void Notify(const std::string& text, bool is_error = false);
    std::string FormatUpdated(double epoch_seconds) const;
    NodeGraph::ProjectStore* ActiveStore();
    bool PassesFilter(const std::string& name) const;

    void* parent_hwnd_ = nullptr;
    Hooks hooks_;
    Mode mode_ = Mode::kPage;
    NodeGraph::ProjectStore* store_ = nullptr;
    // Browser mode fallback: own store when the host didn't provide one.
    std::unique_ptr<NodeGraph::ProjectStore> owned_store_;

    bool children_attached_ = false;
    Theme::Tokens last_tokens_{};

    // Toolbar widgets (built once).
    std::shared_ptr<TextInput> name_input_;
    std::shared_ptr<TextInput> search_input_;
    std::shared_ptr<Button> new_btn_;
    std::shared_ptr<Button> save_as_btn_;
    std::shared_ptr<Button> refresh_btn_;

    // Project rows (rebuilt by Refresh/RebuildRows).
    std::vector<Row> rows_;
    std::vector<std::shared_ptr<Widget>> row_widgets_;
    bool rows_built_ = false;
    Layout::Rect projects_card_{};
    Layout::Rect toolbar_card_{};
    Layout::Rect empty_rect_{};
    // Browser-mode hero header (title + subtitle), empty in kPage mode.
    Layout::Rect browser_title_rect_{};
    Layout::Rect browser_sub_rect_{};
    float content_height_ = 0.0f;

    // Two-click delete armament (avoids a destructive modal).
    std::int64_t armed_delete_id_ = -1;
    std::chrono::steady_clock::time_point armed_until_{};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_PROJECTS_WIDGET_H
