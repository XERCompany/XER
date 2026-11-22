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
#include <unordered_map>

#include "preview_binding.h"      // value members — complete type
#include "shell_prefs.h"
#include "shortcut_router.h"
#include "source_edit.h"          // SourceMarks — per-path mark storage
#include "timeline_model.h"
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

namespace XER::VideoEditor {
// Model-layer history stack — declared here so Ui2 can hold a
// unique_ptr without pulling timeline_history.h into the header.
class TimelineHistory;
}

namespace XER::VideoEditor::Ui2 {

class DockTabGroup;
class LinearBox;
class MonitorPanel;
class StatusStrip;
class TimelineStrip;
class TimelineBridge;

class VeApp final : public XER::UI::Widgets::Widget {
public:
    // `project_path` is stored for later phases — M1 only shows it in
    // the window/status text. `prefs_path` overrides the shell-prefs
    // file (tests pass a temp path; empty → DefaultShellPrefsPath()).
    explicit VeApp(std::string project_path = {},
                   std::filesystem::path prefs_path = {});
    // Out-of-line: unique_ptr members on forward-declared types
    // (TimelineHistory/TimelineBridge) need their complete type at
    // the destructor's instantiation point.
    ~VeApp() override;

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
    // `is_error` marks failure feedback — the status strip has no
    // error styling yet, so it currently only suppresses nothing;
    // kept in the signature so call sites carry the intent.
    void SetStatus(const std::string& text, bool is_error = false);
    // Timeline document access — the shell owns the Project +
    // TimelineHistory; surfaces reach them only through the bridge.
    TimelineBridge* Bridge() const { return bridge_.get(); }
    Project* Document() { return &project_; }
    const Project* Document() const { return &project_; }
    // Seek entry: clamps + commits through the bridge, which echoes
    // the playhead back into the surface via the host seam.
    void Seek(double seconds);
    double PlayheadSeconds() const { return playhead_; }
    // Shell-side undo/redo entry for the Ctrl+Z/Y chords.
    void Undo();
    void Redo();
    void Notify(const std::string& title, const std::string& message);
    void OpenCommandPalette();
    void ShowDialog(const std::string& title, const std::string& body,
                    std::function<void()> on_accept = nullptr);

    // ── M2 monitors + playback ───────────────────────────────
    // The panels own the chrome (transport row, timecode, meters,
    // overlay toggles); the bindings own the PreviewEngine pair.
    // Everything funnels through the surface_hooks.h / panel Hooks
    // contract — the surfaces never touch the engines directly.
    MonitorPanel* ProgramMonitor() const {
        return program_monitor_.get();
    }
    MonitorPanel* SourceMonitor() const {
        return source_monitor_.get();
    }
    PreviewBinding& ProgramPlayback() { return program_engine_; }
    PreviewBinding& SourcePlayback() { return source_engine_; }

    // Rebinds the program document (project load / spec §6.3 "the
    // shell owns document state"): history clears per the load
    // contract and the program binding re-inits on the next
    // transport action (Premiere sequence semantics).
    void SetProject(const Project& project);

    // Source monitor load — the caller supplies probed metadata
    // (media catalog/probe lands with M3's bin); builds the
    // synthetic single-clip project the source engine decodes and
    // restores persisted marks (old OpenInSourceMonitor parity).
    void OpenInSourceMonitor(const std::string& path,
                             const std::string& name,
                             double duration_seconds, int fps_num,
                             int fps_den, bool has_video,
                             bool has_audio, int media_w,
                             int media_h);
    bool SourceLoaded() const { return !source_path_.empty(); }
    const std::string& SourcePath() const { return source_path_; }

    // Program transport — exclusive rule: starting program playback
    // stops the source binding (the engines share the audio
    // endpoint; old StopSourceTransport parity).
    void ProgramTogglePlay();
    void ProgramShuttleStep(int direction);   // J=-1 / L=+1 ladder
    void ProgramShuttleStop();                // K
    void ProgramStepFrame(int direction);     // ±1 frame
    // I/O keys on the program viewer set the work area at the
    // playhead (old SetWorkAreaPointAtPlayhead parity — commits
    // through the bridge so undo + NotifyProjectChanged apply).
    void ProgramSetWorkAreaPoint(bool mark_in);
    // Lift leaves a hole, extract ripples — work-area range through
    // history (old ';' / '\'' parity).
    void ProgramLiftExtract(bool close_gap);
    void StopProgramTransport();

    // Source transport — mirror of the program path against the
    // source binding + synthetic project.
    void SourceTogglePlay();
    void SourceShuttleStep(int direction);
    void SourceShuttleStop();
    void SourceSeek(double seconds);
    void SourceStepFrame(int direction);
    void SourceMark(bool mark_in);            // I / O at playhead
    void SourceClearMark(bool mark_in);       // Alt+I / Alt+O
    // Three-point edits: the marked source range lands on the
    // timeline at the program playhead (insert = ripple, overwrite
    // = replace — old EditSourceToTimeline parity, history macro).
    void EditSourceToTimeline(bool insert);
    void StopSourceTransport();

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
    // M2 timeline document: seeds the default track layout when the
    // project is empty, builds history_/bridge_, and pushes the
    // post-commit snapshot back into the surface (the legacy
    // NotifyProjectChanged).
    void EnsureTimelineDocument();
    void WireTimelineBridge();
    void NotifyTimelineChanged();
    void OnBridgeSeek(double seconds);
    // M2 monitor wiring: builds the panel hooks (transport → the
    // methods above, activation → ShortcutRouter scope, status →
    // SetStatus) and attaches each binding to its panel.
    void WireMonitorPanels();
    // Engine-reported playhead (worker → DrainPending → binding
    // fan-out): updates the doc playhead + timeline surface without
    // routing back through the bridge (which would re-seek).
    void ApplyProgramPlayhead(double seconds);
    void ApplySourcePlayhead(double seconds);

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
    std::shared_ptr<MonitorPanel> source_monitor_;
    std::shared_ptr<MonitorPanel> program_monitor_;
    std::shared_ptr<TimelineStrip> timeline_;
    std::shared_ptr<StatusStrip> status_;

    // ── M2 document state (shell-owned; surfaces only see hooks) ──
    Project project_;
    std::unique_ptr<TimelineHistory> history_;
    std::unique_ptr<TimelineBridge> bridge_;
    double playhead_ = 0.0;
    std::shared_ptr<XER::UI::Widgets::Command> palette_;
    std::shared_ptr<XER::UI::Widgets::Dialog> dialog_;

    // ── M2 monitor/playback state ────────────────────────────
    // Engines lazy-init on the first transport action — headless
    // smoke construction never spawns a decoder. The exclusive
    // playback rule lives in the Program*/Source* methods.
    PreviewBinding program_engine_;
    PreviewBinding source_engine_;
    // Source-monitor document: synthetic one-clip project +
    // per-path marks (old source_*_ parity).
    Project source_project_;
    std::string source_path_;
    std::string source_name_;
    double source_duration_ = 0.0;
    double source_playhead_ = 0.0;
    int source_media_w_ = 0, source_media_h_ = 0;
    int source_fps_num_ = 30, source_fps_den_ = 1;
    bool source_has_video_ = false, source_has_audio_ = false;
    std::unordered_map<std::string, SourceMarks> source_marks_;

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
