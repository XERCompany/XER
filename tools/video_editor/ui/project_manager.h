// Premiere-style "Home" start screen for the XER video editor. This
// is NOT a modal dialog: it lives as a plain full-window child of
// VideoEditorAppWidget, painted over the workspace below the title bar
// and dismissed into the editor (Escape, project open, or project
// create). The host keeps every destructive rule — recents open
// through the same LoadProject/ConfirmDiscardChanges path as File →
// Open, and the widget owns no project state of its own.
//
// Design-system widgets only: Button / Field / TextInput / Select /
// Item / ScrollArea. The recent-project grid is a drawn ScrollArea
// content — the same convention the media bin uses for its card grid,
// since no shared "tile" widget exists.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "records_store.h"  // XER::VideoEditor::Records::RecentProject
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Field;
class Item;
class ScrollArea;
class Select;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class ProjectManagerScreen final : public XER::UI::Widgets::Widget {
public:
    // What the "Create project" button collected — the host validates
    // and owns the actual ProjectStore::Save.
    struct NewProjectRequest {
        std::string name;        // display name / file stem
        int preset_index = 0;    // index into BuiltinSequencePresets()
        std::string directory;   // UTF-8 dir for "<name>.xerproj"
    };
    struct Hooks {
        // Pulled by Open()/Refresh() — the host reads its records
        // store (or returns {} when it never opened).
        std::function<std::vector<Records::RecentProject>()> list_recents;
        // Tile activated (click or Enter): open through the app's
        // guarded project-load path.
        std::function<void(const std::string& utf8_path)> on_open_recent;
        // "Open Project…" — the host runs its file picker.
        std::function<void()> on_open_browse;
        // Tile's × (or right-click): drop the row from recents.
        std::function<void(const std::string& utf8_path)> on_remove_recent;
        // "Create project" — false keeps the screen up (the host
        // pushes the reason through SetStatus).
        std::function<bool(const NewProjectRequest&)> on_create_project;
        // Initial value for the location field.
        std::function<std::string()> default_location;
        // "…" next to the location field — returns "" on cancel.
        std::function<std::string()> pick_location;
        // The templates/demo entry.
        std::function<void()> on_open_demo;
        // Escape / "skip to editor" — the host hides the screen.
        std::function<void()> on_dismiss;
        // Status passthrough so the host can mirror messages onto its
        // own status strip as well.
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit ProjectManagerScreen(Hooks hooks);

    // Shows the screen and re-pulls the recents list (file-existence
    // and time-ago labels are refreshed too).
    void Open();
    // Re-pulls the recents model without touching visibility.
    void Refresh();
    // Hides the screen WITHOUT firing on_dismiss (the host calls it
    // after a successful open/create as well as for the skip path).
    void Dismiss();
    bool IsOpen() const { return IsVisible(); }

    // Bottom-left status line for host-side failures (open/create
    // errors) — errors paint in tokens.danger.
    void SetStatus(const std::string& text, bool is_error = false);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    // Fills its parent's bounds — the shell lays it out over the whole
    // workspace band.
    XER::UI::Layout::Size GetMinSize() const override {
        return {640.0f, 400.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    // Drawn tile-grid widget inside the recents ScrollArea (defined in
    // the .cc — posters, time-ago/duration meta, hover × control).
    class RecentsGrid;

    void EnsureChildren();
    void SubmitCreate();
    // Left/right column rects Layout()/Render() share.
    XER::UI::Layout::Rect LeftColumnRect() const;
    XER::UI::Layout::Rect RightColumnRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    std::string status_text_;
    bool status_is_error_ = false;

    // Left column — actions + the always-expanded new-project form.
    std::shared_ptr<XER::UI::Widgets::Button> open_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::Select> preset_select_;
    std::shared_ptr<XER::UI::Widgets::TextInput> location_input_;
    std::shared_ptr<XER::UI::Widgets::Button> browse_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> create_btn_;
    std::shared_ptr<XER::UI::Widgets::Item> demo_item_;
    // Field wrappers (SetLabel captions) — the controls above live
    // inside these; kept as children so the captions draw.
    std::shared_ptr<XER::UI::Widgets::Field> name_field_;
    std::shared_ptr<XER::UI::Widgets::Field> preset_field_;
    std::shared_ptr<XER::UI::Widgets::Field> location_field_;

    // Right column — scrollable recents grid.
    std::shared_ptr<RecentsGrid> recents_grid_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> recents_scroll_;
    int recents_count_ = 0;   // for the "N projects" caption
};

}  // namespace XER::VideoEditor::Ui
