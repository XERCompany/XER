// XER Studio video editor shell. Premiere/AE-style workspace built on
// XER_UI_Core: media bin + program monitor + inspector + timeline.
// The Project itself stays headless and every edit goes through
// TimelineHistory; preview and export consume snapshots.
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "audio_sync.h"
#include "export_engine.h"
#include "media_catalog.h"
#include "media_prep.h"
#include "project_autosave.h"
#include "source_edit.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "url_import.h"
#include "lab/async_op.h"
#include "playback/preview_engine.h"
#include "color_panel_widget.h"
#include "effects_panel.h"
#include "export_dialog.h"
#include "extensions_panel.h"
#include "export_queue_panel.h"
#include "history_panel.h"
#include "inspector_widget.h"
#include "lab_panel.h"
#include "layers_panel.h"
#include "markers_panel.h"
#include "media_bin_widget.h"
#include "mixer_panel.h"
#include "monitor_widget.h"
#include "nav_rail_widget.h"
#include "new_layer_dialog.h"
#include "plugins_panel_widget.h"
#include "photo_workspace.h"
#include "project_manager.h"
#include "font_manager.h"
#include "settings_dialog.h"
#include "shortcuts_overlay.h"
#include "source_monitor_widget.h"
#include "speed_dialog.h"
#include "status_toast.h"
#include "timeline_widget.h"
#include "title_editor.h"
#include "url_import_dialog.h"
#include "waveform_panel.h"
#include "workspace_layout.h"
#include "vst3_editor.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class AppTitleBar;
class Button;
class Label;
class Tabs;
}

namespace Json { class Value; }

namespace XER::VideoEditor::Ui {

// Bound thunks the window subclass proc reads via its ref_data
// pointer — keeps the Win32 hook (and windows.h types) out of the
// class definition. The same proc that vetoes WM_CLOSE also drains
// WM_DROPFILES, so both callbacks travel in one payload. Both must be
// set before SetWindowSubclass installs the proc.
struct WindowSubclassHooks {
    // WM_CLOSE veto — ConfirmDiscardChanges.
    std::function<bool()> close_guard;
    // WM_DROPFILES — OS file/directory drop -> media import. Paths
    // arrive wide (DragQueryFileW); conversion happens in the callee.
    std::function<void(const std::vector<std::wstring>&)> file_drop;
};

// Clip + horizontal wheel-scroll host for the right-panel tab strip —
// defined in video_editor_app.cc. Widgets::Tabs lays cells out at
// natural text width, which can exceed a narrow docked panel; the
// host clips the strip (ClipsChildren cuts rendering AND hit-testing)
// and scrolls it so every tab stays reachable.
class TabStripScroll;

class VideoEditorAppWidget final : public XER::UI::Widgets::Widget {
public:
    explicit VideoEditorAppWidget(void* parent_hwnd,
                                  std::string project_path = {});
    ~VideoEditorAppWidget() override;

    XER::UI::Widgets::AppTitleBar* TitleBar() const { return title_bar_.get(); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    // Headless capture seam (`--screenshot --ui`): seeks the playhead
    // to `seconds`, spins the program preview engine up and pumps
    // Tick() until the first composited frame has been marshalled
    // into the program monitor (bounded by `timeout_ms`). Engine or
    // audio-sink failures degrade to the monitor's empty placeholder
    // and return false — the caller still gets a valid chrome/layout
    // capture. Call AFTER the widget has joined a WidgetTree (the
    // Seek/SetPlayhead fan-out expects attached children).
    bool PrepareUiCapture(double seconds, int timeout_ms = 3000);

    // CEF "editor.*" shell bridge entry point (xer-ext:// extension
    // pages only — shell_commands.cc forwards editor.* commands
    // here). Called from a CEF worker thread: parks the request in
    // shell_reqs_ and blocks until Tick() runs it through
    // DispatchExtensionAction (or a timeout kicks in). Returns the
    // action's JSON result string.
    std::string EnqueueEditorShellRequest(const std::string& action,
                                          const std::string& args);

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
    void EnsureChildren();
    void InstallGlobalShortcuts();
    // Tree-level right-click dispatcher: hit-tests the main panels and
    // forwards to each widget's ShowContextMenu(). Registered once on
    // the WidgetTree from EnsureChildren().
    bool OnContextMenu(const XER::UI::Widgets::Event& event);
    void BuildTitleBar();
    // Re-pushes File/Edit/Timeline/View onto the title bar. Called
    // once from BuildTitleBar() and again whenever menu state the
    // items snapshot could go stale (selection-dependent Timeline
    // items, the autosave-recovery offer).
    void RebuildTitleBarMenus();
    // Shared "Project Settings…" presenter (center pill, nav rail,
    // File menu, avatar menu all land here).
    void ShowProjectSettingsDialog();
    // ---- Home start screen (project_manager.h) --------------------
    // Non-modal full-window child shown at launch when no project
    // path was supplied and via File ▸ Home. Escape/close dismisses
    // into the editor.
    void ShowStartScreen();
    void HideStartScreen();
    // Records-store touch for the current project_path_ — called by
    // LoadProject/SaveProject so opens AND first saves land in
    // recents with name/duration/poster metadata.
    void TrackRecentProject();
    // PM "Create project": unique "<name>.xerproj" under `directory`
    // at the chosen sequence preset — NewProject() reset (discard
    // guard inside) + ProjectStore::Save through SaveProject().
    bool CreateProjectFromStartScreen(
        const ProjectManagerScreen::NewProjectRequest& req);
    // PM template entry: an unsaved fully-generated sequence (bars +
    // matte + title) so users can explore without importing media.
    void CreateDemoProject();
    // Initial value for the PM location field: "<user-data>/XER/
    // VideoEditor/Projects" — a sibling of records.db/catalog.db.
    std::string DefaultProjectDirectory() const;
    bool NewProject();
    void OpenProject();
    bool LoadProject(const std::string& utf8_path);
    bool SaveProject(bool choose_path = false);
    void ImportMedia();
    // Media-bin/File "Import from URL…" — shows the URL dialog;
    // StartUrlImport runs the download on url_op_ and Tick() lands
    // the file through the shared catalog-import path.
    void ImportFromUrl();
    void StartUrlImport(const std::string& url);
    // Per-file catalog import shared by the file dialog and OS drops:
    // Open + Import + selection update. No status-bar writes, no
    // RefreshCatalog/poster requests — callers batch those and own
    // the status wording.
    bool ImportMediaFile(const std::string& path, std::string* error);
    // WM_DROPFILES thunk body (window subclass): expands dropped
    // directories one level deep, filters to the shared media
    // extension list, then batch-imports what remains.
    void OnOsFilesDropped(const std::vector<std::wstring>& wide_paths);
    // Batch catalog import + summary status for OnOsFilesDropped —
    // "Importing N files…" up front, "imported N, skipped M" after.
    void ImportDroppedPaths(const std::vector<std::string>& paths,
                            int skipped);
    void AddSelectedToTimeline();
    // Inserts the catalog entry for `path` at the playhead — the
    // media-bin context menu passes its row path; falls back to the
    // bin selection when the path isn't in the catalog.
    void AddMediaToTimeline(const std::string& path);
    // Same placement rules as AddMediaToTimeline but at an explicit
    // timeline position — the media-bin drag->drop path lands the
    // clip under the cursor instead of at the playhead.
    // preferred_track >= 0 wins when its kind matches the media and
    // it is unlocked (bin drop on an empty lane); a mismatched or
    // locked target falls back to the first free track of the kind.
    void AddMediaToTimelineAt(const std::string& path,
                              double at_seconds,
                              int preferred_track = -1);
    // Media-bin drag hooks: on_drop fires when a card drag releases
    // over the timeline bounds (clip lands at the drop time);
    // on_drag_hover drives the timeline's drop-target affordance.
    void OnBinMediaDrop(const std::string& path,
                        XER::UI::Layout::Point position);
    void OnBinDragHover(XER::UI::Layout::Point position, bool active);
    void StartExport();          // opens the configured export dialog
    void BeginConfiguredExport();  // dialog "Export" -> engine start
    // Shared export kick-off: engine start + queue row + option
    // capture for RetryExport. Callers guard IsRunning().
    void EnqueueExport(const ExportOptions& options);
    void RetryExport(int job);   // re-issue a Failed row's export
    void AddTrack(TrackKind kind);
    void SelectClip(const std::string& clip_id);
    void Seek(double seconds);
    void TogglePlay();
    // Pauses the program transport (engine + monitor flags + shuttle
    // ladder + toolbar label) — shared by TogglePlay's pause branch,
    // ShuttleStop and the source monitor's exclusive-playback rule.
    void StopProgramTransport();
    // ---- source monitor (Premiere-style clip preview) ----
    // Loads a bin clip into the source monitor: builds the synthetic
    // single-clip project the source PreviewEngine decodes, restores
    // the clip's persisted marks, shows the monitor and makes it the
    // keyboard-transport target.
    void OpenInSourceMonitor(const std::string& path);
    void EnsureSourceEngine();              // lazy Init on the source project
    void SourceSeek(double seconds);
    void SourceTogglePlay();
    void SourceShuttleStep(int direction);  // JKL mirror of ShuttleStep
    void SourceShuttleStop();
    void StopSourceTransport();
    void SourceMark(bool mark_in);          // I / O at the source playhead
    void SourceClearMark(bool mark_in);     // Alt+I / Alt+O
    // Three-point edits: the marked source range lands on the timeline
    // at the playhead — insert ripples (InsertClipAt), overwrite
    // replaces (OverwriteClipAt).
    void EditSourceToTimeline(bool insert);
    // Keyboard transport routing: source monitor owns keys while it
    // was the last monitor interacted with (mirrors Premiere's
    // last-active-viewer rule).
    bool SourceActive() const;
    void ApplyClip(const Clip& clip);
    // AI media actions (media-bin context menu): run on one shared
    // background job, results land as markers via Tick().
    void DetectBeatsForMedia(const std::string& path);
    void TranscribeMedia(const std::string& path);
    // Audio-plugin bounce: decode PCM -> apply plugin effect -> write
    // a sibling WAV -> catalog import lands via the AI result poll.
    void BounceAudioFx(const std::string& path,
                       const std::string& plugin_id);
    // AI "Find Objects" (ai_tags.h / YA.AIFindObject): scans the
    // clip's consumed source span on the shared AI job; detected
    // ClipTag ranges land on the clip via ReplaceClip in Tick().
    // With no model installed the status line carries the install
    // hint instead of failing silently.
    void FindObjectsForClip(const std::string& clip_id);
    void ClearClipTags(const std::string& clip_id);
    void MoveClip(const std::string& clip_id, double start);
    // One-click waveform sync (audio_sync.h): correlate every
    // audio-bearing clip's envelope against the selected/first clip
    // and apply matches as history-wrapped moves. Runs on sync_op_ —
    // the UI stays live so the Cancel affordance can abort a long
    // multi-cam decode.
    void SyncAudioClips();
    // Cancels the in-flight audio sync: flips the flag the in-process
    // plan polls AND kills the disposable XERSyncMedia worker (a
    // remote request can't be polled mid-flight — KillWorker fails
    // the RoundTrip fast with no crash-death penalty). Bound to the
    // status-strip Cancel button and the Esc key.
    void RequestSyncCancel();
    void TrimClip(const std::string& clip_id, TrimEdge edge,
                  double point);
    void SplitClip(const std::string& clip_id, double at_seconds);
    void DeleteClip(const std::string& clip_id);
    // Transition commits — the pickers pass an explicit type/duration;
    // duration <= 0 falls back to transition_duration_. Every applied
    // pair also becomes the default for Ctrl+D / the junction menu.
    void AddTransitionAfter(const std::string& clip_id,
                            Transition::Type type, double duration,
                            const std::string& script = "");
    void AddTransitionPair(const std::string& from_id,
                           const std::string& to_id,
                           Transition::Type type, double duration,
                           const std::string& script = "");
    // Ctrl+D: the stored default transition lands on the junction
    // nearest the playhead (falls back to the selected clip's
    // out-edge junction when the playhead sits nowhere near a cut).
    void ApplyDefaultTransition();
    // JKL shuttle: PreviewEngine::SetRate-driven NLE transport on the
    // ±{1,2,4,8}x ladder — same-direction presses double (capped),
    // the opposite key steps back down and flips direction at 1x.
    void ShuttleStep(int direction);   // +1 = L forward, -1 = J reverse
    void ShuttleStop();                // K — pause + reset the ladder
    // Q/W: ripple-trim the head/tail of the clip at the playhead (on
    // a gap: Q extends the next clip's head back, W the previous
    // clip's tail forward).
    void RippleTrimToPlayhead(bool trim_head);
    // ,/. — move the selected clip (plus its link group) ±1 frame.
    void NudgeSelection(int direction);
    // Phase-2 edit ops (timeline tool commits -> history commands).
    void RippleDeleteClipById(const std::string& clip_id);
    void RippleTrimClip(const std::string& clip_id, TrimEdge edge,
                        double point);
    void RollingEditClips(const std::string& from_id,
                          const std::string& to_id, double delta);
    void SlipClipBy(const std::string& clip_id, double delta_src);
    void SlideClipBy(const std::string& clip_id, double delta_tl);
    void SetClipRateBy(const std::string& clip_id, double rate);
    void SetClipKeyframesBy(const std::string& clip_id, KeyProp prop,
                            std::vector<Keyframe> keys);
    void AddClipEffectBy(const std::string& clip_id,
                         const ClipEffect& effect);
    void AddMarkerAt(double t, const std::string& name);
    void RemoveMarkerAt(double t);
    void RenameMarkerAt(double t, const std::string& name);
    void SetWorkAreaRange(double in_s, double out_s);
    void SetTrackFlag(int track, char which, bool on);  // 'm','h','l'
    void SetRightTab(int tab);                          // 0..10
    void ApplyProjectSettings(const ProjectSettings& settings);
    // "New Item" placements: a generated clip on the target video
    // track at the playhead (menu) or an explicit lane+time (timeline
    // context menu). track < 0 picks TargetVideoTrack(); t < 0 the
    // playhead; empty name/param fall back to the kind defaults.
    void AddGeneratedClip(SourceKind kind);
    void AddGeneratedClipAt(SourceKind kind, int track, double at_seconds,
                            const std::string& name,
                            const std::string& param, double duration);
    // Track for "New Item" inserts: the selected clip's track when it
    // is an unlocked video lane, else the first unlocked video track,
    // else -1 (caller creates one). Same rule for the LAYERS panel's
    // active-track display.
    int TargetVideoTrack() const;
    // Color matte modal (new_layer_dialog.h): pending_layer_*
    // captures the context-menu insert point so Apply lands on the
    // lane/time that opened the dialog.
    void ShowNewLayerDialog(int track, double at_seconds);
    // "Add Media…" picker: imports the file then places it on
    // `preferred_track` at `at_seconds` (the AddMediaToTimelineAt
    // fallback rules apply).
    void ShowAddMediaDialog(int preferred_track, double at_seconds);
    void AddVst3FxToSelectedClip();
    // Open clip.audio_fx[fx_index]'s native editor; fx_index < 0
    // picks the first enabled fx (menu path). The popup session is
    // tracked in vst3_editor_clip_/vst3_editor_fx_ so edits commit
    // back on close.
    void AddScriptAudioFxToSelectedClip(const std::string& script_id);
    void OpenVst3FxEditor(const std::string& clip_id, int fx_index);
    // on_closed handler for the editor popup: harvest the session's
    // edited parameters and ReplaceClip them into the model (chain
    // re-syncs + project file persists them). No-op when the bound
    // clip/fx is gone or nothing changed.
    void CommitVst3EditorParams();
    bool RunLabOperation(const LabPanel::RunParams& params,
                         const Lab::JobControl& job);
    void Undo();
    void Redo();
    void ToggleShortcutsOverlay();
    void ApplyWorkspace(const WorkspaceLayout& layout);
    // The "Photo" workspace preset is special-cased: instead of a
    // panel arrangement it swaps the whole content band for the
    // PhotoWorkspaceWidget (the M3 photo-editor UI over the photo/
    // Document+BrushEngine+PhotoHistory core). Any other preset drops
    // back to the video layout.
    void EnterPhotoMode();
    void ExitPhotoMode();
    void RefreshWaveformPanel();
    void RefreshExportQueue();
    void InitAutosave();
    // Startup crash-recovery offer: detects autosave slots for the
    // current project (or ./autosave next to the CWD for an unsaved
    // session) and arms the File ▸ Recover Autosave item — it never
    // loads silently.
    void CheckAutosaveRecovery();
    void RecoverAutosave();
    void OpenSpeedDialog();
    void OpenTitleEditor();
    // System font browser (Tools > Fonts…); applies the chosen face
    // to the selected Text clip's style param.
    void OpenFontManager();
    void ApplyClipSpeed(const std::string& clip_id, double rate,
                        bool ripple);
    void AddAdjustmentClip();
    void RefreshCatalog();
    void NotifyProjectChanged();
    // Marker keyboard nav — PageUp/PageDown plus Premiere's Shift+M.
    // The search wraps so the keys always land on a marker.
    void JumpToMarker(bool next);
    // Unsaved-changes gate for the destructive ops (New/Open/close):
    // returns true when the current project may be discarded — clean,
    // or the user picked Discard / completed the save in the prompt.
    bool ConfirmDiscardChanges();
    void SetStatus(const std::string& text, bool is_error = false);
    // Action dispatcher behind xer_ext.call()/projectInfo()/setStatus()
    // — JSON in/out, scoped surface for extension scripts.
    std::string DispatchExtensionAction(const std::string& action,
                                        const std::string& arg_json);
    // assets/extensions/ as a UTF-8 dir string ("" when the assets
    // layer never resolved one) — shared by the boot scan, the panel
    // refresh hook and the menu's folder/rescan items.
    std::string ExtensionsDir() const;
    // Menu/panel entry points: open `id`'s declared UI in the Ext
    // tab's CEF view, re-run `id`'s entry point, rescan the dir
    // (ExtHost::Scan + panel Reload + menu rebuild), Explorer-reveal
    // the extensions dir.
    void OpenExtensionUi(const std::string& id);
    void RunExtensionById(const std::string& id);
    void RescanExtensions();
    void RevealExtensionsFolder();
    // ---- out-of-process native extensions (VE.ExtensionsRuntime) --
    // ext_loads every verified package carrying a native payload
    // (Extension::has_native) into the runtime, unloading the stale
    // resident set first. The job runs on ext_op_ — NEVER a
    // synchronous UI-thread RoundTrip: extension code can ve_call
    // back into the editor, and only a free UI thread can service
    // the host_call request that produces (see worker_pipe.h's
    // callback_env contract).
    void QueueNativeExtensionLoads();
    // Fires one ext_call at a resident native extension; the result +
    // events land through the next Tick pump (status strip).
    void CallNativeExtension(const std::string& ext_id,
                             const std::string& fn,
                             const std::string& args_json);
    // Tick hook: pops the finished ext job, drains + answers queued
    // host_call requests (the ve_call reply path — MUST run even
    // while a job is in flight), and throttles an ext_events poll
    // for events extension-owned threads queued between calls.
    void PumpExtensionRuntime(double dt_seconds);
    // One host_call line -> dispatch -> reply. UI thread.
    void ServiceExtHostCall(const std::string& line);
    // The editor service table extensions reach through ve_call —
    // JSON result string per the {"ok":...} result contract.
    std::string DispatchExtHostCall(const std::string& name,
                                    const std::string& args_json);
    // Applies a drained event array: log/status surface on the
    // status strip, add_clip lands through the same
    // AddGeneratedClipAt/AddMediaToTimelineAt paths the menus use.
    void ApplyExtEvents(const Json::Value& events);
    void ApplyExtAddClip(const std::string& ext_id,
                         const Json::Value& event);
    MediaBounds BoundsForPath(const std::string& path) const;
    const CatalogEntry* FindCatalogEntry(const std::string& path) const;

    void* parent_hwnd_ = nullptr;
    std::string project_path_;
    Project project_;
    TimelineHistory history_;
    MediaCatalog catalog_;
    // Recent-projects records store (records_store.h) — app-owned, UI
    // thread. Opened next to the catalog in the ctor; an open failure
    // degrades the start screen to "no recents" instead of blocking.
    Records::RecentProjectsStore records_;
    // Media-prep worker pool (media_prep.h): poster/peaks decodes +
    // import-poster extraction off the UI thread. Declared right after
    // the catalog it borrows so the dtor joins its workers before the
    // catalog dies. Timeline hooks are non-blocking lookups into it.
    MediaPrep media_prep_{&catalog_};
    std::vector<CatalogEntry> catalog_entries_;
    std::string selected_media_path_;
    std::string selected_clip_id_;
    double playhead_ = 0.0;
    bool snap_enabled_ = true;
    bool left_panel_visible_ = true;
    bool right_panel_visible_ = true;
    // Monitor focus mode: both side panels hidden, restored on exit.
    bool monitor_focus_ = false;
    bool saved_left_panel_ = true;
    bool saved_right_panel_ = true;
    bool children_attached_ = false;
    bool shortcuts_installed_ = false;
    bool context_menu_registered_ = false;
    // Chrome lines cached by Layout() for the structural strips
    // Render() paints (status bar surface, timeline divider). The
    // right-panel tab strip draws itself now — Widgets::Tabs paints
    // its own bar, hairline and underline indicator.
    float status_bar_top_ = 0.0f;
    float timeline_top_ = 0.0f;
    // User-resizable panels: Layout() fills these 6px hit-rects inside
    // the gap strips the child layout leaves between panels (left
    // panel's right edge / right panel's left edge / timeline's top
    // edge); HandleEvent() runs the drag. The tree's left-button
    // mouse-capture keeps MouseMove routed here once a press lands.
    XER::UI::Layout::Rect left_splitter_rect_{};
    XER::UI::Layout::Rect right_splitter_rect_{};
    XER::UI::Layout::Rect timeline_splitter_rect_{};
    int drag_splitter_ = -1;   // 0 = left, 1 = right, 2 = timeline
    int hover_splitter_ = -1;
    float drag_start_ = 0.0f;       // cursor x or y at press
    float drag_start_size_ = 0.0f;  // panel width/height at press
    // User size overrides — 0 falls back to the manifest/proportional
    // defaults. Seeded from SharedLayoutConfig() in EnsureChildren().
    float left_width_ = 0.0f;
    float right_width_ = 0.0f;
    float timeline_height_ = 0.0f;
    // Last resolved sizes, cached by Layout() for drag-start deltas.
    float last_left_w_ = 0.0f;
    float last_right_w_ = 0.0f;
    float last_timeline_h_ = 0.0f;

    // Shared background job for AI media actions (beats / transcribe).
    // The worker fills ai_result_ under the mutex; Tick() pops the
    // AsyncOp result and lands markers on the UI thread.
    Lab::AsyncOp ai_op_;
    struct AiResult {
        // Rich span payload for the ai.* extension services: beats
        // fill (t, t, "Beat"), transcribe fills (t0, t1, text),
        // find-objects fills the ClipTag ranges. `markers` stays the
        // flat (time, name) legacy view Tick() applies to the project.
        struct Span {
            double t0 = 0.0;
            double t1 = 0.0;
            std::string label;
            float confidence = 0.0f;
        };
        std::string kind;  // "beats" | "transcribe" | "objects" | "bounce"
        std::vector<std::pair<double, std::string>> markers;
        std::vector<Span> spans;
        std::string detail;
        std::string bounce_path;   // audio-plugin bounce output (WAV)
        // AI find-objects payload: the target clip + its new tag set.
        // tags_clip_id non-empty selects this path in Tick().
        std::string tags_clip_id;
        std::vector<ClipTag> tags;
    };
    std::mutex ai_result_mutex_;
    AiResult ai_result_;
    // Snapshot of the most recently finished job — ai.last serves it
    // to extensions so they can build on the async result (e.g.
    // caption clips from the transcript spans) after polling
    // ai.status to completion.
    AiResult ai_last_;
    float ai_progress_shown_ = -1.0f;

    // CEF "editor.*" shell bridge (src/cef/shell_commands.cc):
    // xer-ext:// extension pages call xer.call("editor.<action>") —
    // the command lands on a CEF worker thread while every app action
    // must run on the UI thread. Requests park here as promises;
    // Tick() drains the queue through DispatchExtensionAction and
    // fulfils them. The hook itself lives in video_editor_app.cc.
    struct ShellRequest {
        std::string action;
        std::string args;
        std::shared_ptr<std::promise<std::string>> reply;
    };
    std::mutex shell_req_mutex_;
    std::deque<ShellRequest> shell_reqs_;

    // URL-download background job — same AsyncOp/marshal pattern as
    // ai_op_: the worker only writes url_result_/url_status_ under the
    // mutex; Tick() pops the result and runs the catalog import on this
    // thread (ImportMediaFile is not thread-safe — it touches
    // selected_media_path_ and status).
    Lab::AsyncOp url_op_;
    std::mutex url_result_mutex_;
    UrlImport::Result url_result_;
    std::string url_status_;          // last worker status line
    float url_progress_shown_ = -1.0f;

    // Audio-sync background job — the same AsyncOp/marshal pattern as
    // ai_op_ but a dedicated slot so a sync never evicts an AI task.
    // The worker reads a project snapshot (UI edits keep flowing
    // while it runs); the report lands in sync_report_ via Tick().
    // Declared after media_prep_ so the joining dtor runs while the
    // prep pool is still alive.
    Lab::AsyncOp sync_op_;
    std::mutex sync_result_mutex_;
    AudioSyncReport sync_report_;
    std::atomic<bool> sync_cancel_{false};
    std::atomic<int> sync_progress_done_{0};
    std::atomic<int> sync_progress_total_{0};
    int sync_progress_shown_ = -1;

    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> undo_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> redo_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> split_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> delete_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> snap_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> play_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> export_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_label_;
    // Right-docked in the status strip — the "Syncing audio…
    // [Cancel]" affordance; hidden whenever sync_op_ is idle.
    std::shared_ptr<XER::UI::Widgets::Button> sync_cancel_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> zoom_label_;
    // Ordered toolbar row (button + slot width). EnsureChildren fills
    // it from SharedAssetsConfig().toolbar_ids order when the assets
    // layer loaded a toolbar.json — unknown action ids get disabled
    // placeholder buttons — compiled order otherwise. Layout() walks it.
    std::vector<
        std::pair<std::shared_ptr<XER::UI::Widgets::Button>, float>>
        toolbar_row_;

    std::shared_ptr<NavRailWidget> nav_rail_;
    std::shared_ptr<MediaBinWidget> media_bin_;
    std::shared_ptr<MonitorWidget> monitor_;
    std::shared_ptr<SourceMonitorWidget> source_monitor_;
    std::shared_ptr<InspectorWidget> inspector_;
    std::shared_ptr<EffectsPanelWidget> effects_panel_;
    std::shared_ptr<PluginsPanelWidget> plugins_panel_;
    std::shared_ptr<ColorPanelWidget> color_panel_;
    // Extensions dock tab (replaces the old "Web" tab): owns the
    // embedded CEF surface its xer-ext:// pages render into.
    std::shared_ptr<ExtensionsPanelWidget> extensions_panel_;
    std::shared_ptr<MarkersPanelWidget> markers_panel_;
    std::shared_ptr<LabPanel> lab_panel_;
    std::shared_ptr<TimelineWidget> timeline_;
    std::shared_ptr<ExportDialog> export_dialog_;
    std::shared_ptr<SettingsDialog> settings_dialog_;
    std::shared_ptr<HistoryPanelWidget> history_panel_;
    std::shared_ptr<MixerPanelWidget> mixer_panel_;
    std::shared_ptr<ShortcutsOverlay> shortcuts_overlay_;
    std::shared_ptr<SpeedDialog> speed_dialog_;
    std::shared_ptr<UrlImportDialog> url_dialog_;
    std::shared_ptr<TitleEditor> title_editor_;
    std::shared_ptr<FontManager> font_manager_;
    std::shared_ptr<NewLayerDialog> new_layer_dialog_;
    std::shared_ptr<LayersPanelWidget> layers_panel_;
    // Context-menu insert point the matte dialog applies to —
    // track < 0 / time < 0 means the default (TargetVideoTrack /
    // playhead) was in effect when the dialog opened.
    int pending_layer_track_ = -1;
    double pending_layer_time_ = -1.0;
    std::shared_ptr<ExportQueuePanelWidget> export_queue_;
    std::shared_ptr<WaveformPanelWidget> waveform_panel_;
    std::shared_ptr<StatusToastWidget> status_toast_;
    // Photo workspace (ui/photo_workspace.*): fills the content band
    // below the title bar while photo_mode_ is on — video chrome is
    // hidden, Ctrl+Z/Y/S/O/N route to the photo document instead.
    std::shared_ptr<PhotoWorkspaceWidget> photo_workspace_;
    bool photo_mode_ = false;
    // Home start screen — the LAST child so it paints/hit-tests above
    // the workspace (title bar stays on top of it in layout).
    std::shared_ptr<ProjectManagerScreen> project_manager_;
    // Right-panel tab strip: Widgets::Tabs in Underline mode inside
    // the clip/wheel-scroll host — see TabStripScroll above.
    std::shared_ptr<XER::UI::Widgets::Tabs> right_tabs_;
    std::shared_ptr<TabStripScroll> tab_strip_;
    // 0 Insp, 1 FX, 2 Mkr, 3 Lab, 4 Hist, 5 Mix, 6 Queue, 7 Plugins,
    // 8 Color, 9 Ext, 10 Lyr
    int right_tab_ = 0;
    double timeline_fraction_ = 0.32;  // workspace layout knob
    std::vector<ExportJobRow> export_jobs_;
    // Index-aligned with export_jobs_ — the options each row was
    // queued with, so Retry re-issues the exact same export.
    std::vector<ExportOptions> export_job_options_;
    AutosaveManager autosave_;        // initialized once a path exists
    double autosave_poll_ = 0.0;      // Tick accumulator (seconds)
    // Rate-limiter for the media-prep queue-full poster-drop warning —
    // drops are counted by MediaPrep and surfaced coalesced here at
    // most once per few seconds.
    std::chrono::steady_clock::time_point poster_drop_warned_at_{};
    // Pending autosave-recovery offer (CheckAutosaveRecovery arms it,
    // RecoverAutosave consumes it). Kept outside the manager so an
    // unsaved session can still point at a ./autosave dir.
    bool autosave_offer_ = false;
    std::string autosave_offer_dir_;
    std::string autosave_offer_project_;
    // Unsaved-changes tracking — NotifyProjectChanged() sets it, a
    // successful save or a New/Load project swap clears it. Deliberately
    // NOT AutosaveManager::IsDirty(): a written autosave slot still
    // means the .xerproj on disk is stale.
    bool project_dirty_ = false;
    // Bound thunks the WM_CLOSE/WM_DROPFILES subclass proc invokes
    // through its ref_data pointer — keeps the Win32 hook (and
    // windows.h types) out of this header. Set before
    // SetWindowSubclass in the ctor.
    WindowSubclassHooks window_hooks_;
    // Last drop-hover state pushed to the timeline — avoids an
    // Invalidate() per MouseMove during a bin drag.
    bool bin_drop_hovering_ = false;
    ExportEngine export_engine_;

    // PreviewEngine drives playback frames + audio meters; the monitor
    // renders them through its external-playback path. Callbacks fire
    // on the engine worker — they only fill pending_* slots under the
    // mutex and the app Tick marshals them onto widgets.
    class EngineSink;
    std::unique_ptr<EngineSink> engine_sink_;
    // Platform audio output (WASAPI on Windows) — ONE instance per
    // engine. A shared single sink let both engine workers call
    // IAudioSink::Write concurrently (exclusive transport only pauses
    // the other engine's flag; its worker can still be inside a
    // blocking Write during the switch), and IAudioRenderClient's
    // GetBuffer/ReleaseBuffer is not thread-safe — concurrent calls
    // corrupt the padding accounting and silence the session for
    // good. Two independent shared-mode streams on the same endpoint
    // have no shared state; Windows mixes them. Declared before the
    // engines so they outlive them — engines only borrow the pointer.
    std::unique_ptr<IAudioSink> program_audio_sink_;
    std::unique_ptr<IAudioSink> source_audio_sink_;
    std::chrono::steady_clock::time_point audio_sink_retry_at_{};
    bool audio_sink_tried_ = false;
    // Forwarding sink the engines hold for their whole lifetime —
    // PreviewOptions::audio_sink is captured once at Init, so a device
    // that appears LATER (retry path, headphones plugged in) could
    // never reach an already-initialized engine through the raw
    // pointer. The binding is atomic: the engine worker can be inside
    // Write while the UI thread swaps the target.
    class LateBindingAudioSink : public IAudioSink {
    public:
        void Bind(IAudioSink* target) {
            target_.store(target, std::memory_order_release);
        }
        bool Write(const std::int16_t* pcm, int frames,
                   int rate) override {
            IAudioSink* t = target_.load(std::memory_order_acquire);
            return t != nullptr && t->Write(pcm, frames, rate);
        }
        std::string LastError() const override {
            IAudioSink* t = target_.load(std::memory_order_acquire);
            return t != nullptr ? t->LastError()
                                : std::string("audio device not open");
        }
    private:
        std::atomic<IAudioSink*> target_{nullptr};
    };
    LateBindingAudioSink program_sink_binding_;
    LateBindingAudioSink source_sink_binding_;
    PreviewEngine preview_engine_;
    bool preview_ready_ = false;
    bool preview_dirty_ = true;   // project changed since last Init
    // Monitor context-menu pick (1.0 / 0.5 / 0.25). Init resets the
    // engine's quality command, so EnsurePreviewEngine re-applies it.
    float preview_quality_ = 1.0f;
    // JKL shuttle state: 0 = not shuttling; sign = direction with
    // magnitude on the {1,2,4,8}x ladder. Reset by K, Space and the
    // engine's pause-at-end.
    double shuttle_rate_ = 0.0;
    // "Default transition" state — seeded to Premiere's Crossfade @1s
    // and re-armed by every applied transition pick.
    Transition::Type transition_type_ = Transition::Type::Crossfade;
    double transition_duration_ = 1.0;
    std::string transition_script_;   // last-used Script id (Ctrl+D)
    std::mutex preview_mutex_;
    std::vector<std::uint8_t> pending_frame_;
    int pending_frame_w_ = 0, pending_frame_h_ = 0;
    bool has_pending_frame_ = false;
    float pending_peak_l_ = 0.0f, pending_peak_r_ = 0.0f;
    bool has_pending_peaks_ = false;
    double pending_playhead_ = -1.0;

    // Lazily creates the per-engine platform audio sink into `which`
    // (program or source). Failed opens no longer latch — retry is
    // throttled to once per few seconds across both sinks.
    void EnsurePlatformAudioSink(std::unique_ptr<IAudioSink>* which);
    void EnsurePreviewEngine();
    void OnEngineFrame(const video::Frame& frame);
    void OnEnginePeaks(float l, float r);
    void OnEnginePlayhead(double t);

    // ---- source monitor ----
    // A second PreviewEngine decoding a synthetic one-clip Project on
    // the loaded media file — same frame/audio path as the program
    // monitor, its callbacks marshalled through source_*_pending
    // slots by Tick(). The sink is declared before the engine so the
    // engine (Shutdown in its dtor) dies first.
    enum class MonitorTarget { Program, Source };
    MonitorTarget monitor_target_ = MonitorTarget::Program;
    class SourceEngineSink;
    std::unique_ptr<SourceEngineSink> source_sink_;
    PreviewEngine source_engine_;
    bool source_ready_ = false;
    Project source_project_;               // synthetic 1-clip project
    std::string source_path_;              // UTF-8 media path ("" = none)
    std::string source_name_;              // basename for the header
    double source_duration_ = 0.0;
    int source_media_w_ = 0, source_media_h_ = 0;
    int source_fps_num_ = 30, source_fps_den_ = 1;
    bool source_has_video_ = true;
    bool source_has_audio_ = false;   // probe/catalog audio-stream flag
    double source_playhead_ = 0.0;
    bool source_playing_ = false;
    bool source_visible_ = false;
    double source_shuttle_rate_ = 0.0;
    // Per-clip marks keyed by normalized media path — survives
    // reloading the clip (Premiere keeps marks with the master clip).
    std::unordered_map<std::string, SourceMarks> source_marks_;
    std::mutex source_mutex_;
    std::vector<std::uint8_t> source_pending_frame_;
    int source_pending_w_ = 0, source_pending_h_ = 0;
    bool source_has_frame_ = false;
    double source_pending_playhead_ = -1.0;

    void OnSourceEngineFrame(const video::Frame& frame);
    void OnSourceEnginePlayhead(double t);

    // Popup-window VST3 plugin editor session (ui/vst3_editor.h) +
    // the clip/fx it is bound to for param commit-back on close.
    Vst3PluginEditor vst3_editor_;
    std::string vst3_editor_clip_;
    int vst3_editor_fx_ = -1;

    // Lab ops run on the panel's worker; the produced PNG is stashed
    // here and imported+added to the timeline by the next Tick.
    std::mutex lab_mutex_;
    std::string pending_lab_png_;
    bool has_pending_lab_ = false;
    // SmartSelect payload: the lab worker stashes the produced mask
    // under lab_mutex_; Tick moves it into lab_mask_ — the stored
    // selection the next Cutout/Remove/ToLayer run consumes via
    // RunParams::mask (on_run_prepare copies it on the UI thread).
    std::shared_ptr<const std::vector<std::uint8_t>>
        pending_lab_mask_;
    int pending_lab_mask_w_ = 0, pending_lab_mask_h_ = 0;
    bool has_pending_lab_mask_ = false;
    std::shared_ptr<const std::vector<std::uint8_t>> lab_mask_;
    int lab_mask_w_ = 0, lab_mask_h_ = 0;

    // ---- native extension bridge (VE.ExtensionsRuntime.exe) -------
    // ext_op_ serializes every request that EXECUTES extension code
    // (ext_load/ext_call/ext_unload). The worker's reply carries the
    // drained event array; the job thread writes ext_outcome_ under
    // ext_mutex_ and Tick marshals it (same pattern as ai_op_).
    Lab::AsyncOp ext_op_;
    std::mutex ext_mutex_;
    struct ExtOutcome {
        bool apply_loaded = false;   // reload job: swap resident ids
        std::vector<std::string> loaded_ids;
        std::string summary;         // status-strip text on success
        // Serialized JSON array — kept as TEXT so this header stays
        // free of <json/json.h>; Tick parses before ApplyExtEvents.
        std::string events_json = "[]";
        std::string call_result;     // ext_call result_json
    };
    ExtOutcome ext_outcome_;              // under ext_mutex_
    // ext_ids currently resident in the runtime (UI thread only).
    std::vector<std::string> native_ext_ids_;
    double ext_poll_ = 0.0;               // ext_events pump accumulator
};

}  // namespace XER::VideoEditor::Ui
