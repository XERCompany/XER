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
#include "ai/ai_bounce.h"         // Ai::AiBounceResult — bounce jobs
#include "ai/whisper_transcribe.h"  // Ai::TranscriptSegment — transcript cache
#include "drag_session.h"         // BinDragSession — value member
#include "preview_binding.h"      // value members — complete type
#include "export_dialog.h"        // ExportRequest — map value member
#include "project_autosave.h"     // AutosaveManager — value member
#include "shell_prefs.h"
#include "shortcut_router.h"
#include "source_edit.h"          // SourceMarks — per-path mark storage
#include "surface_hooks.h"        // MonitorGizmoTarget — gizmo seam
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
class Compositor;        // compositor.h — gizmo live-preview renderer
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
class ExportDialog;          // export_dialog.h — settings form
class ProjectSettingsDialog; // project_settings_dialog.h
struct ExportRequest;        // export_dialog.h — submit payload
struct Ui2ProjectSettings;   // project_settings_dialog.h

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

    // ── project lifecycle (save/open/new + close guard) ─────
    // Unsaved-changes flag — set by every document mutation that
    // funnels through NotifyTimelineChanged, cleared by
    // Save/New/Load (legacy project_dirty_ parity).
    bool IsProjectDirty() const { return project_dirty_; }
    const std::string& ProjectPath() const { return project_path_; }
    // ProjectStore::Save to project_path_ (or a picked path when
    // choose_path / unbound). Clears the dirty flag on success.
    bool SaveProject(bool choose_path = false);
    // The File-menu/Ctrl+S entry — Windows save-as picker when the
    // project is untitled (or choose_path).
    bool SaveProjectInteractive(bool choose_path);
    // Headless save seam for the smoke harness — binds the path then
    // runs the same ProjectStore::Save as the interactive path (no
    // picker, no guard prompt).
    bool SaveProjectAs(const std::string& utf8_path);
    // File ▸ New: close-guarded reset to the seeded default document.
    void NewProject();
    // Raw .xerproj load (no guard, no picker) — the headless seam.
    // Clears history per the ProjectStore load contract, binds
    // autosave + checks for a newer autosave slot afterwards.
    bool LoadProject(const std::string& utf8_path);
    // File ▸ Open: close-guarded GetOpenFileNameW -> LoadProject.
    void OpenProjectInteractive();
    // Window-close seam — wire to Window::SetCloseGuard. Returns true
    // when the close may proceed now; a dirty+unguarded close shows
    // the save/discard/cancel prompt and returns false — the approved
    // choice then re-triggers on_close_approved_ (the host calls
    // Window::Close() from it) so the same WM_CLOSE veto lets it
    // through via close_approved_.
    bool RequestClose();
    void SetOnCloseApproved(std::function<void()> callback);
    // Unsaved-changes prompt state + headless button press — the
    // smoke drives the same code path the dialog's buttons do.
    bool CloseGuardPromptOpen() const { return close_guard_open_; }
    enum class CloseGuardChoice { Save, Discard, Cancel };
    void ResolveCloseGuard(CloseGuardChoice choice);
    // Shared "save before destructive op" prompt — proceed runs on
    // Save (after the save lands) or Discard; Cancel drops it.
    void RequestCloseGuard(std::function<void()> proceed);

    // ── application dialogs ─────────────────────────────────
    // File ▸ Export… and the queue's "+ Enqueue" both land here.
    void ShowExportDialog();
    // File ▸ Project Settings… — apply commits the document fields
    // through history (undoable) and the app fields into prefs_.
    void ShowProjectSettingsDialog();
    // Live form accessors — non-null only while shown (headless
    // discovery/validation seam; both hang off dialog_ overlays).
    ExportDialog* ExportForm() const { return export_form_.get(); }
    ProjectSettingsDialog* SettingsForm() const {
        return settings_form_.get();
    }
    // The form's Export-button path — validates, enqueues, closes.
    // Public so the headless smoke drives the identical code the
    // dialog's Accept result runs (no synthetic queue rows).
    void SubmitExportForm();
    // Composes a queue row + the ExportOptions the driver consumes
    // (per-job store keyed by job id — see WireQueuePanel). False +
    // error_out on validation failure; nothing enqueues then.
    bool EnqueueExportJob(const ExportRequest& request,
                          std::string* error_out = nullptr);
    // The settings dialog's Apply path — document fields ride a
    // SetProjectSettingsCommand through history (undoable), app
    // fields commit to prefs_ + the live autosave policy. Public so
    // the headless smoke drives the same commit the dialog runs.
    void ApplyProjectSettings(const Ui2ProjectSettings& settings);

    // ── autosave ────────────────────────────────────────────
    // AutosaveManager bound after a successful Save/Load to
    // <project dir>/autosave — the Tick pump is gated on
    // prefs_.autosave_enabled + the manager's own interval.
    void SetAutosaveIntervalMs(std::int64_t interval_ms) {
        autosave_.SetIntervalMs(interval_ms);
    }
    bool AutosaveInitialized() const {
        return autosave_.IsInitialized();
    }
    std::vector<std::string> AutosaveSlots() const {
        return autosave_.ListAutosaves();
    }
    // Crash-recovery offer state — armed by CheckAutosaveRecovery
    // (runs at ctor + after LoadProject when a slot is NEWER than
    // the project file). Accept loads through AutosaveManager::
    // LoadLatest; decline just drops the offer.
    bool AutosaveRecoveryOffered() const { return autosave_offer_; }
    const std::string& AutosaveOfferPath() const {
        return autosave_offer_project_;
    }
    void AcceptAutosaveRecovery();
    void DeclineAutosaveRecovery();

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
    // `at_override` >= 0 pins the destination to a context-menu
    // point instead of the playhead. The destination track comes
    // from the surface's V/A patching badges — no targeted track of
    // the source's kind errors, a patched-off audio leg drops
    // silently (Premiere/Resolve patching semantics).
    void EditSourceToTimeline(bool insert, double at_override = -1.0);
    // Replace edit (Premiere "Replace with Clip from Source"): the
    // clip under the playhead on a targeted track — or `target`
    // when the context menu names one — is swapped for the marked
    // source range through history_->ReplaceClip (the timeline
    // footprint/effects/link_group stay; a short source clamps to
    // its media bounds).
    void SourceReplaceEdit(const Clip* target = nullptr);
    // Fit to fill (Resolve "Fit to Fill"): the marked source range
    // retimes into the work area on the targeted track — rate =
    // source span / fill span, one history macro via
    // OverwriteClipAt. No work area -> the targeted clip under the
    // playhead's span is the fill range.
    void SourceFitToFill();
    // Timeline context-menu verb dispatcher (on_source_verb hook):
    // kInsert/kOverwrite at `point`, kReplace onto `clip_id`,
    // kFitToFill into the work area.
    void RunSourceVerb(Ui2::SourceEditVerb verb, double point,
                       const std::string& clip_id);
    // Top-and-tail (Premiere Q/W): ripple-trim the clip under the
    // playhead on the targeted track — head=true moves its start to
    // the playhead, head=false its end (history_->RippleTrimClip
    // closes the freed gap).
    void ProgramTopTail(bool head);
    void StopSourceTransport();

    // Held-combo + play-around transports (the router's held-key
    // contract): while 'k' is held, J/L presses run a fixed slow
    // shuttle instead of the ladder (Premiere K+J/K+L); Shift+K
    // plays around the playhead and pauses at the post-roll edge.
    void ProgramHeldShuttle(int direction);   // K-held J/L, ±1
    void SourceHeldShuttle(int direction);
    void EndHeldShuttle(int direction);       // 0=K up, ±1=J/L up
    void ProgramPlayAround();                 // Shift+K
    void SourcePlayAround();
    // Shift+I/Shift+O — go to in/out (work-area edges on program,
    // marks on source — Premiere "Go to In/Out Point").
    void ProgramGoToWorkAreaEdge(bool mark_in);
    void SourceGoToMark(bool mark_in);
    // F — match frame: the timeline clip under the playhead loads
    // into the source monitor at the matching source time.
    void ProgramMatchFrame();
    static constexpr double kHeldShuttleRate = 0.25;
    static constexpr double kPlayAroundPre = 3.0;
    static constexpr double kPlayAroundPost = 3.0;

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
    // Last-active-viewer scope: the router's active scope and the
    // monitor accent borders move together (single seam for
    // on_activate hooks + OpenInSourceMonitor).
    void ActivateViewerScope(const char* scope);
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
    // (ApplyProjectSettings moved to the public dialogs section —
    // it IS the dialog's commit path, and the smoke drives it.)
    // Close-guard core: `proceed` runs when the op may continue
    // (clean doc → immediately; dirty → only via the prompt).
    // `for_window_close` additionally fires on_close_approved_ so
    // the host re-posts the real close.
    void RunCloseGuard(std::function<void()> proceed,
                       bool for_window_close);
    void DoNewProject();
    // AutosaveManager binding (<project dir>/autosave) + the Tick
    // pump — prefs_.autosave_* gate both.
    void InitAutosave();
    void PumpAutosave(double dt_seconds);
    // Newer-autosave detection after ctor/LoadProject — arms the
    // offer + the File ▸ Recover Autosave item and shows the
    // restore prompt dialog.
    void CheckAutosaveRecovery();
    void SyncAutosaveBadge();
    // Transcript worker drain — Tick pushes progress/result into the
    // panel + cache.
    void PumpTranscriptJob();
    // "Auto-blur faces" (ctx menu): kicks the async YuNet scan for
    // clip_id; PumpFaceBlurJob drains the result slot in Tick and
    // commits object_track + builtin:xer.fx.face_blur through
    // history_->ReplaceClip in one undoable macro.
    void StartFaceBlur(const std::string& clip_id);
    void PumpFaceBlurJob();
    // "Track Mask (AI)…" (clip ctx menu): arms the program monitor's
    // click-point seam — the next frame click is the SAM2 prompt
    // (track_mask_clip_id_ remembers which clip). The click maps
    // into source-frame fractions (ai/sam2_track.h
    // CanvasPointToSourceFraction) and BeginTrackMaskJob launches
    // the async per-frame re-prompt scan; PumpTrackMaskJob drains
    // the mutex slot in Tick and commits mask_track +
    // builtin:xer.fx.track_mask through history_->ReplaceClip.
    // ClearMaskTrack empties the track through the same history
    // path (the effect stays — it no-ops on an empty track).
    void StartTrackMask(const std::string& clip_id);
    void BeginTrackMaskJob(const Clip& clip, float fx, float fy,
                           double src_seconds);
    void PumpTrackMaskJob();
    void ClearMaskTrack(const std::string& clip_id);
    // "AI Upscale 4x" (bin ctx) + "Smooth slow-mo (AI)" (clip ctx):
    // one shared async bounce slot (both are seconds-per-frame CPU
    // work — serializing is honest). The worker runs
    // ai/ai_bounce.h RunVideoBounce and writes ONLY the
    // mutex-guarded slot; PumpAiBounceJob drains it in Tick — a bin
    // bounce imports the produced file into the catalog, a clip
    // bounce additionally repoints the clip's source at the baked
    // file through history_->ReplaceClip (rate -> rate*N, which is 1
    // exactly for a clean 1/N slow-down).
    void StartAiUpscale(const std::string& path);
    void StartAiSlowmo(const std::string& clip_id);
    void PumpAiBounceJob();
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

    // ── program-monitor transform gizmo (WireMonitorPanels) ──
    // The surface pulls the selection's gizmo target per paint;
    // presses that miss the box hit-test the topmost clip under the
    // cursor; gesture hooks bracket live previews (in-memory only —
    // rendered through gizmo_preview_comp_, never history) with ONE
    // ReplaceClip commit inside a BeginMacro/EndMacro pair.
    MonitorGizmoTarget GizmoTargetForSelection() const;
    // Topmost clip whose warped rect contains the frame-normalized
    // point at the playhead ("" = none). Video tracks top→bottom;
    // hidden tracks skipped.
    std::string GizmoPickClip(float nx, float ny) const;
    // clip source px for the gizmo rect — catalog dims for media,
    // canvas dims for generated/compound/adjustment sources.
    bool GizmoClipSourceSize(const Clip& clip, int* w, int* h) const;
    void GizmoPreviewTransform(const std::string& clip_id,
                               const Transform& transform);
    void CommitGizmoTransform(const std::string& clip_id,
                              const Transform& transform);

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
    // K-held combo state: which J/L direction the gesture is
    // shuttling (0 = none) and which binding owns it. The router's
    // on_up path ends it when K or the direction key releases.
    int held_shuttle_dir_ = 0;
    bool held_shuttle_source_ = false;
    // Program-monitor gizmo live-preview renderer — one Compositor
    // per gesture (Init at on_gizmo_begin, Shutdown at end). Its
    // bound project snapshot is mutated through UpdateClipTransform
    // so preview frames render with warm decoders; project_ itself
    // is untouched until the single commit lands.
    std::unique_ptr<Compositor> gizmo_preview_comp_;
    bool gizmo_preview_ok_ = false;
    // Shift+K play-around: the post-roll time the Tick pump pauses
    // at (-1 = idle) and which binding owns the gesture.
    double playaround_until_ = -1.0;
    bool playaround_source_ = false;

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
    // Per-job encoder settings captured at EnqueueExportJob time —
    // StartQueuedExport reads them back (jobs enqueued by other
    // paths, e.g. smoke rows, fall back to ExportOptions defaults).
    std::unordered_map<std::string, ExportRequest> export_requests_;
    // Last submitted request — seeds the dialog's next open so the
    // user tweaks instead of retyping (session-only, like the legacy
    // dialog's remembered fields).
    ExportRequest last_export_request_{};

    // ── lifecycle state ────────────────────────────────────
    bool project_dirty_ = false;
    XER::VideoEditor::AutosaveManager autosave_;
    double autosave_poll_ = 0.0;
    // Crash-recovery offer (CheckAutosaveRecovery arms it; the
    // restore prompt + the File ▸ Recover item consume it).
    bool autosave_offer_ = false;
    std::string autosave_offer_dir_;
    std::string autosave_offer_project_;
    // Close guard: the pending prompt's proceed-thunk + the host's
    // "really close now" callback (RunGui binds it to
    // window->Close()). close_approved_ lets the second WM_CLOSE
    // pass the same guard.
    bool close_guard_open_ = false;
    bool close_guard_for_window_ = false;
    bool close_approved_ = false;
    std::function<void()> close_guard_proceed_;
    std::function<void()> on_close_approved_;
    std::shared_ptr<XER::UI::Widgets::Dialog> close_guard_dialog_;
    // Live dialog forms (null when hidden) — the shared_ptr keeps
    // them valid for headless drivers between show and result.
    std::shared_ptr<ExportDialog> export_form_;
    std::shared_ptr<ProjectSettingsDialog> settings_form_;
    std::shared_ptr<XER::UI::Widgets::Dialog> export_dialog_;
    std::shared_ptr<XER::UI::Widgets::Dialog> settings_dialog_;
    std::shared_ptr<XER::UI::Widgets::Dialog> autosave_dialog_;
    // Save-as basename suggestion from the settings dialog's name
    // field (untitled projects only — bound paths keep their name).
    std::string project_name_;

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

    // "Auto-blur faces" async YuNet job — same marshalling contract
    // as the whisper worker: the thread writes ONLY the
    // mutex-guarded slot, Tick() drains it via PumpFaceBlurJob into
    // the clip's object_track + xer.fx.face_blur effect commit.
    std::thread face_worker_;
    std::atomic<bool> face_cancel_{false};
    std::atomic<float> face_progress_{0.0f};
    std::atomic<bool> face_running_{false};
    std::mutex face_result_mutex_;
    bool face_result_ready_ = false;
    bool face_result_ok_ = false;
    ClipTrack face_result_;
    std::string face_result_error_;
    std::string face_job_clip_id_;

    // "Track Mask (AI)" armed-pick + async SAM2 job — same
    // marshalling contract as the face worker (worker writes ONLY
    // the mutex-guarded slot; Tick drains it). track_mask_armed_
    // shares the click seam with color_pick_armed_ — first writer
    // wins; both can't be armed at once.
    bool track_mask_armed_ = false;
    std::string track_mask_clip_id_;
    std::thread mask_worker_;
    std::atomic<bool> mask_cancel_{false};
    std::atomic<float> mask_progress_{0.0f};
    std::atomic<bool> mask_running_{false};
    std::mutex mask_result_mutex_;
    bool mask_result_ready_ = false;
    bool mask_result_ok_ = false;
    ClipMaskTrack mask_result_;
    std::string mask_result_error_;
    std::string mask_job_clip_id_;

    // AI bounce jobs (upscale + slow-mo share ONE slot — both are
    // seconds-per-frame CPU work, so serializing them is the honest
    // behavior). Same marshalling contract as the face/whisper
    // workers.
    std::thread bounce_worker_;
    std::atomic<bool> bounce_cancel_{false};
    std::atomic<float> bounce_progress_{0.0f};
    std::atomic<bool> bounce_running_{false};
    std::mutex bounce_result_mutex_;
    bool bounce_result_ready_ = false;
    bool bounce_result_ok_ = false;
    XER::VideoEditor::Ai::AiBounceResult bounce_result_;
    std::string bounce_result_error_;
    // Clip-bounce bookkeeping — an empty clip id marks a bin bounce
    // (upscale); a set one is the slow-mo repoint target.
    std::string bounce_job_clip_id_;
    double bounce_job_rate_ = 1.0;
    int bounce_job_factor_ = 1;

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
