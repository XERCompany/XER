// ui2 application shell (M1) — the rebuild-spec §5 workspace skeleton
// composed entirely from the shared widget kit plus the ui2
// composites in shell_widgets.h:
//
//   ┌──────────────────────────────────────────────────────────┐
//   │ AppTitleBar — workspace switcher · menus · command pill · │
//   │              panel toggles · theme · window controls      │
//   ├──────┬──────────────┬───────────────────┬───────────────┤
//   │ nav  │ left dock    │ monitors          │ right dock    │
//   │ rail │ (Tabs: Media │ (Resizable pair:  │ (Tabs:        │
//   │      │  Effects     │  Source|Program)  │  Inspector,   │
//   │      │  Audio Text  │                   │  Color, Mark, │
//   │      │  Transitions)│                   │  Hist, Mix,   │
//   │      │              │                   │  Queue, Ext,  │
//   │      │              │                   │  Scopes, Trn) │
//   ├──────┴──────────────┴───────────────────┴───────────────┤
//   │ TimelineStrip — tool row + tracks surface placeholder   │
//   ├─────────────────────────────────────────────────────────┤
//   │ StatusStrip — status text + autosave/jobs/proxy badges  │
//   └─────────────────────────────────────────────────────────┘
//
// The two Resizable splits (outer vertical, top horizontal, monitor
// pair) persist resolved fractions through SetOnLayout → ShellPrefs
// (ui2_shell.json). Workspace presets flip dock collapse state, the
// active dock tabs and the timeline share.
//
// M1 deliberately wires NO document state: every dock panel is a
// PlaceholderPanel, monitor/timeline are Surface stand-ins, and the
// shortcut bindings post status/toast feedback instead of touching
// TimelineHistory. M2/M3 swap the placeholders for real surfaces
// behind the surface_hooks.h contracts — the shell shape and the
// router stay.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "shell_prefs.h"
#include "shortcut_router.h"
#include "workspace_layout.h"
#include "widgets/widget.h"

namespace XER::UI::Core {
class Window;
}
namespace XER::UI::Widgets {
class AppTitleBar;
class Command;
class Dialog;
class Resizable;
class Sidebar;
}

namespace XER::VideoEditor::Ui2 {

class DockTabGroup;
class LinearBox;
class StatusStrip;
class TimelineStrip;

class VeApp final : public XER::UI::Widgets::Widget {
public:
    // `project_path` is stored for later phases — M1 only shows it in
    // the window/status text. `prefs_path` overrides the shell-prefs
    // file (tests pass a temp path; empty → DefaultShellPrefsPath()).
    explicit VeApp(std::string project_path = {},
                   std::filesystem::path prefs_path = {});

    // ── structure accessors (headless-discovery contract) ────
    XER::UI::Widgets::AppTitleBar* TitleBar() const;
    XER::UI::Widgets::Sidebar* NavRail() const;
    DockTabGroup* LeftDock() const;
    DockTabGroup* RightDock() const;
    XER::UI::Widgets::Resizable* TopSplit() const;
    XER::UI::Widgets::Resizable* OuterSplit() const;
    XER::UI::Widgets::Resizable* MonitorSplit() const;
    TimelineStrip* Timeline() const;
    StatusStrip* Status() const;
    ShortcutRouter& Shortcuts() { return shortcuts_; }

    // ── workspace + layout state ─────────────────────────────
    void ApplyWorkspace(const WorkspaceLayout& ws);
    bool ApplyWorkspaceByName(const std::string& name);
    const std::string& CurrentWorkspace() const { return workspace_name_; }
    bool LeftDockVisible() const { return left_visible_; }
    bool RightDockVisible() const { return right_visible_; }
    bool StatusVisible() const { return status_visible_; }
    double TimelineFraction() const { return timeline_fraction_; }
    void SetLeftDockVisible(bool visible);
    void SetRightDockVisible(bool visible);
    void SetStatusVisible(bool visible);

    // ── persistence ──────────────────────────────────────────
    const ShellPrefs& Prefs() const { return prefs_; }
    // Writes prefs_ to prefs_path_ immediately (Tick() also flushes
    // ~1 s after the last splitter change). Returns the error string
    // on failure.
    std::string FlushPrefs();

    // ── status / feedback ────────────────────────────────────
    void SetStatus(const std::string& text);
    void Notify(const std::string& title, const std::string& message);
    void OpenCommandPalette();
    void ShowDialog(const std::string& title, const std::string& body,
                    std::function<void()> on_accept = nullptr);

    // ── widget contract ──────────────────────────────────────
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    // Window frame callback entry (same role as the old shell's
    // TickTree): pumps the widget tree once the app is attached.
    void TickTree(double dt_seconds);

    // Installs the shortcut router on the hosting tree — idempotent.
    // Called lazily from Layout(); the smoke harness may call it
    // directly after SetRoot().
    void InstallShortcutRouter();

    XER::UI::Layout::Size GetMinSize() const override {
        return {960.0f, 620.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1440.0f, 860.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void BuildShell();
    void BuildTitleBar();
    void RebuildMenus();
    void BuildDockPanels();
    void BindShellShortcuts();
    void RebuildCommandItems();
    // Rebuilds the top-row Resizable panels so restored/captured
    // fractions apply (Resizable has no post-hoc size setter —
    // ClearPanels + AddPanel reuses the same child widgets).
    void RebuildOuterSplit();
    void SyncDockCollapsed();
    void MarkPrefsDirty();
    void CapturePrefsFromSplits();
    // Applies prefs_ fields (visibility/tabs/monitor mode) to the
    // live widgets — used at boot after LoadShellPrefs.
    void ApplyPrefsToShell();

    std::string project_path_;
    std::filesystem::path prefs_path_;
    ShellPrefs prefs_;
    bool prefs_dirty_ = false;
    double prefs_flush_timer_ = 0.0;

    ShortcutRouter shortcuts_;
    bool shortcuts_installed_ = false;

    // ── shell structure ──────────────────────────────────────
    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<XER::UI::Widgets::Sidebar> nav_rail_;
    std::shared_ptr<LinearBox> top_row_;   // rail + top_split_ band
    std::shared_ptr<DockTabGroup> left_dock_;
    std::shared_ptr<DockTabGroup> right_dock_;
    std::shared_ptr<XER::UI::Widgets::Resizable> top_split_;
    std::shared_ptr<XER::UI::Widgets::Resizable> outer_split_;
    std::shared_ptr<XER::UI::Widgets::Resizable> monitor_split_;
    std::shared_ptr<XER::UI::Widgets::Widget> source_monitor_;
    std::shared_ptr<XER::UI::Widgets::Widget> program_monitor_;
    std::shared_ptr<TimelineStrip> timeline_;
    std::shared_ptr<StatusStrip> status_;
    std::shared_ptr<XER::UI::Widgets::Command> palette_;
    std::shared_ptr<XER::UI::Widgets::Dialog> dialog_;

    // ── layout state (mirrors prefs_) ────────────────────────
    std::string workspace_name_ = "Editing";
    bool left_visible_ = true;
    bool right_visible_ = true;
    bool status_visible_ = true;
    bool source_monitor_visible_ = true;
    bool monitors_tabbed_ = false;
    double timeline_fraction_ = 0.32;
};

}  // namespace XER::VideoEditor::Ui2
