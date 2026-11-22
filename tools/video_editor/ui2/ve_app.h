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

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "theme/color.h"          // Theme::Color — pick seam
#include "ai/whisper_transcribe.h"  // Ai::TranscriptSegment — transcript cache
#include "drag_session.h"         // BinDragSession — value member
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
// M3 media stack — unique_ptr members, complete at ve_app.cc.
class MediaCatalog;
class MediaPrep;
class ProxyManager;
class ExportEngine;      // export_engine.h — single-job async worker
struct CatalogEntry;     // media_catalog.h — bin row
}

namespace XER::VideoEditor::Ui2 {

class DockTabGroup;
class LinearBox;
class MonitorPanel;
class StatusStrip;
class TimelineStrip;
class TimelineBridge;
class MediaPanel;
class EffectsPanel;
class InspectorPanel;
class ColorPanel;
class ScopesPanel;
class MixerPanel;
class MarkersPanel;
class HistoryPanel;
class QueuePanel;
class ExtensionsPanel;
class TranscriptPanel;
struct ColorParams;          // color_panel.h — commit payload
struct FxEntry;              // effects_panel.h — catalog row
struct InspectorParamSpec;   // inspector_panel.h — param editor spec

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
    // Photo workspace — created lazily when the "Photo" preset is
    // applied; replaces the monitor split as the top-split center.
    class PhotoWorkspace* PhotoWorkspaceView() const;
    PreviewBinding& ProgramPlayback() { return program_engine_; }
    PreviewBinding& SourcePlayback() { return source_engine_; }

    // Rebinds the program document (project load / spec §6.3 "the
    // shell owns document state"): history clears per the load
    // contract and the program binding re-inits on the next
    // transport action (Premiere sequence semantics).
    void SetProject(const Project& project);

    // ── M3 panels (structure accessors — smoke/discovery) ────
    MediaPanel* MediaBin() const { return media_panel_.get(); }
    EffectsPanel* FxBrowser() const { return effects_panel_.get(); }
    InspectorPanel* Inspector() const { return inspector_panel_.get(); }
    ColorPanel* Color() const { return color_panel_.get(); }
    ScopesPanel* Scopes() const { return scopes_panel_.get(); }
    MixerPanel* Mixer() const { return mixer_panel_.get(); }
    // M3 wave-3 right-dock panels.
    MarkersPanel* Markers() const { return markers_panel_.get(); }
    HistoryPanel* History() const { return history_panel_.get(); }
    QueuePanel* ExportQueue() const { return queue_panel_.get(); }
    ExtensionsPanel* Extensions() const {
        return extensions_panel_.get();
    }
    TranscriptPanel* Transcript() const {
        return transcript_panel_.get();
    }

    // M3 media stack — the bin's Import button and the File menu both
    // funnel here. `ImportMediaFiles` is the headless seam (no file
    // dialog); the interactive picker wraps it on _WIN32.
    void ImportMediaFiles(const std::vector<std::string>& paths);
    void ImportMediaInteractive();
    // History-backed bin→timeline insert (legacy AddMediaToTimelineAt
    // parity): kind-matched track placement, linked A/V pairs, snap,
    // macro + rollback on failure. preferred_track < 0 -> first free.
    void AddMediaToTimelineAt(const std::string& path,
                              double at_seconds,
                              int preferred_track = -1);
    void AddMediaToTimeline(const std::string& path) {
        AddMediaToTimelineAt(path, playhead_);
    }
    // The timeline selection as Clip copies — the inspector's
    // provider contract (single-selection surface for now).
    std::vector<Clip> SelectedClips() const;
    void RefreshInspector();

    // ── M3 wave-3 transcript subject ──────────────────────────
    // Binds the transcript panel to a media path (bin-bound mode —
    // words render/edit-actions stay disabled because nothing maps
    // to the timeline) or to a timeline clip (full edit-by-
    // transcript). The selection-driven path calls the clip form
    // from the bridge's on_select.
    void BindTranscriptTo(const std::string& media_path,
                          const std::string& display_name);
    void BindTranscriptToClip(const std::string& clip_id);
    // Kicks the async whisper job for the bound media (the panel's
    // on_transcribe hook lands here). No-op while a job runs or the
    // backend is unavailable — status reports which.
    void StartTranscription();
    bool TranscriptRunning() const { return transcript_running_; }
    // Test seam: inject a finished transcript for the bound media
    // (cache + panel feed) without a worker — the smoke's fake
    // transcript path.
    void InjectTranscript(
        const std::vector<XER::VideoEditor::Ai::TranscriptSegment>&
            segments);

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
    // ── M3 panel/media wiring ─────────────────────────────────
    // Opens the catalog (DefaultDbPath) and builds the MediaPrep
    // pool + ProxyManager on first call — deferred from the ctor so
    // headless smoke construction never spawns workers until the
    // panels actually exist.
    void EnsureMediaStack();
    void WireMediaPanel();
    void WireEffectsPanel();
    void WireInspectorPanel();
    // M3 wave-2 dock panels: Color / Scopes / Mix.
    void WireColorPanel();
    void WireScopesPanel();
    void WireMixerPanel();
    // M3 wave-3 dock panels: Markers / History / Queue / Extensions /
    // Transcript — hook wiring in ve_app.cc; all document mutations
    // leave through history_ / bridge_.
    void WireMarkersPanel();
    void WireHistoryPanel();
    void WireQueuePanel();
    void WireExtensionsPanel();
    void WireTranscriptPanel();
    // Push seams — NotifyTimelineChanged/Tick fan-outs.
    void RefreshMarkersPanel();
    void RefreshHistoryPanel();
    void RefreshTranscriptSubject();
    // ExtHost scan → ExtensionRow feed; lazy so smoke construction
    // never scans. Rescans when `force`.
    void RefreshExtensionsPanel(bool force = false);
    std::string ExtensionsDir() const;
    // Export-queue driver: on_submit lands here — starts the real
    // ExportEngine for the head job; Tick polls Progress()/Status().
    void StartQueuedExport(const std::string& job_id);
    void PumpExportQueue(double dt_seconds);
    // Transcript worker drain — Tick pushes progress/result into the
    // panel + cache.
    void PumpTranscriptJob();
    // ColorPanel::on_color_changed — upserts the color-fx param
    // snapshot onto the clip (temperature/tint/lift_gamma_gain/
    // channel_mixer — see color_panel.h for the param→fx mapping)
    // and commits through history_->ReplaceClip (coalesced).
    void CommitClipColor(const std::string& clip_id,
                         const ColorParams& params);
    // White-balance eyedropper: samples the program monitor's
    // current frame (5x5 mean) at the frame-normalized point the
    // armed click seam delivered. No frame -> error status, pick
    // still disarms.
    XER::UI::Theme::Color SampleProgramFrame(float nx, float ny) const;
    // catalog_.List() -> media_panel_->SetEntries — the bin's only
    // data channel (ConsumeCatalogDirty drain also lands here).
    void RefreshBinEntries();
    const CatalogEntry* FindCatalogEntry(const std::string& path) const;
    // Inspector/effects FX metadata providers (resolved against
    // BuiltinFx/PluginHost/JsFxRuntime + the core-type table).
    std::vector<InspectorParamSpec> FxParamSpecs(
        const std::string& fx_type) const;
    std::string FxDisplayName(const std::string& fx_type) const;
    // EffectsPanel::on_apply_fx — appends ClipEffect{type, defaults}
    // to each clip inside one history macro.
    void ApplyFxToClips(const std::vector<std::string>& clip_ids,
                        const std::string& fx_type);
    // InspectorPanel::on_commit_clips — ReplaceClip each through
    // history (coalesced per clip); a >1 clip vector wraps in a macro.
    void CommitInspectorClips(const std::vector<Clip>& clips);
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
    // Photo workspace band swapped into the top split's center slot
    // while the "Photo" preset is active (photo_mode_).
    std::shared_ptr<class PhotoWorkspace> photo_workspace_;
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

    // ── M3 panels + media stack (shell-owned; panels see hooks) ──
    std::shared_ptr<MediaPanel> media_panel_;
    std::shared_ptr<EffectsPanel> effects_panel_;
    std::shared_ptr<InspectorPanel> inspector_panel_;
    std::shared_ptr<ColorPanel> color_panel_;
    std::shared_ptr<ScopesPanel> scopes_panel_;
    std::shared_ptr<MixerPanel> mixer_panel_;
    // White-balance pick state — the color panel's on_arm_pick arms
    // the program monitor's click-point seam; the armed click lands
    // in WireMonitorPanels' on_click_point and routes here.
    bool color_pick_armed_ = false;
    // Borrowing order matters: media_prep_/proxies_ borrow catalog_,
    // so declaration order catalog_ first keeps destruction sound
    // (reverse: proxies_ -> media_prep_ -> catalog_).
    std::unique_ptr<XER::VideoEditor::MediaCatalog> catalog_;
    std::unique_ptr<XER::VideoEditor::MediaPrep> media_prep_;
    std::unique_ptr<XER::VideoEditor::ProxyManager> proxies_;
    BinDragSession drag_session_;
    bool media_stack_ready_ = false;
    bool media_stack_warned_ = false;
    // Cached effects catalog (BuildFxRegistryEntries is cheap but
    // walks the registries — built once at wire time).
    std::vector<XER::VideoEditor::Ui2::FxEntry> fx_entries_;

    // ── M3 wave-3 panels + drivers (shell-owned) ─────────────
    std::shared_ptr<MarkersPanel> markers_panel_;
    std::shared_ptr<HistoryPanel> history_panel_;
    std::shared_ptr<QueuePanel> queue_panel_;
    std::shared_ptr<ExtensionsPanel> extensions_panel_;
    std::shared_ptr<TranscriptPanel> transcript_panel_;

    // Export queue driver — ExportEngine is single-job (no pause,
    // no built-in queue), so the shell owns one engine plus the id
    // of the queue row it currently drives. Tick polls it; pause/
    // resume report unsupported through the hooks.
    std::unique_ptr<XER::VideoEditor::ExportEngine> export_engine_;
    std::string export_active_id_;
    // Row submitted while the engine was still draining a cancelled
    // job — PumpExportQueue resubmits it once the worker frees.
    std::string export_pending_id_;
    double export_elapsed_ = 0.0;
    int export_serial_ = 0;

    // Extension session state — ExtHost has no enable/disable
    // concept (extensions_panel.h seam doc), so the shell keeps the
    // disabled set and gates Run/OpenUi on it. Session-only.
    std::unordered_set<std::string> disabled_extensions_;
    bool ext_scanned_ = false;

    // Transcript subject + async whisper job. The worker writes only
    // the mutex-guarded result slot; Tick() drains it into the panel
    // + the per-media cache (legacy StoreTranscriptResult parity).
    std::string transcript_media_path_;
    std::string transcript_media_name_;
    std::string transcript_clip_id_;
    std::unordered_map<std::string,
                       std::vector<XER::VideoEditor::Ai::TranscriptSegment>>
        transcript_cache_;
    std::string transcript_caption_preset_ = "classic";
    std::thread transcript_worker_;
    std::atomic<bool> transcript_cancel_{false};
    std::atomic<float> transcript_progress_{0.0f};
    std::atomic<bool> transcript_running_{false};
    std::mutex transcript_result_mutex_;
    bool transcript_result_ready_ = false;
    bool transcript_result_ok_ = false;
    std::vector<XER::VideoEditor::Ai::TranscriptSegment>
        transcript_result_;
    std::string transcript_result_error_;
    std::string transcript_job_path_;

    // ── layout state (mirrors prefs_) ────────────────────────
    std::string workspace_name_ = "Editing";
    bool left_visible_ = true;
    bool right_visible_ = true;
    bool status_visible_ = true;
    bool source_monitor_visible_ = true;
    bool monitors_tabbed_ = false;
    bool photo_mode_ = false;   // "Photo" preset: center = photo band
    double timeline_fraction_ = 0.32;

    void EnsurePhotoWorkspace();
};

}  // namespace XER::VideoEditor::Ui2
