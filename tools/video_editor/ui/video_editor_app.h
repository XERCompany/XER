// XER Studio video editor shell. Premiere/AE-style workspace built on
// XER_UI_Core: media bin + program monitor + inspector + timeline.
// The Project itself stays headless and every edit goes through
// TimelineHistory; preview and export consume snapshots.
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "export_engine.h"
#include "media_catalog.h"
#include "media_prep.h"
#include "project_autosave.h"
#include "source_edit.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "lab/async_op.h"
#include "playback/preview_engine.h"
#include "browser_panel_widget.h"
#include "color_panel_widget.h"
#include "effects_panel.h"
#include "export_dialog.h"
#include "export_queue_panel.h"
#include "history_panel.h"
#include "inspector_widget.h"
#include "lab_panel.h"
#include "markers_panel.h"
#include "media_bin_widget.h"
#include "mixer_panel.h"
#include "monitor_widget.h"
#include "nav_rail_widget.h"
#include "plugins_panel_widget.h"
#include "settings_dialog.h"
#include "shortcuts_overlay.h"
#include "source_monitor_widget.h"
#include "speed_dialog.h"
#include "status_toast.h"
#include "timeline_widget.h"
#include "title_editor.h"
#include "waveform_panel.h"
#include "workspace_layout.h"
#include "vst3_editor.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class AppTitleBar;
class Button;
class Label;
}

namespace XER::VideoEditor::Ui {

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
    void NewProject();
    void OpenProject();
    bool LoadProject(const std::string& utf8_path);
    bool SaveProject(bool choose_path = false);
    void ImportMedia();
    void AddSelectedToTimeline();
    // Inserts the catalog entry for `path` at the playhead — the
    // media-bin context menu passes its row path; falls back to the
    // bin selection when the path isn't in the catalog.
    void AddMediaToTimeline(const std::string& path);
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
    void MoveClip(const std::string& clip_id, double start);
    // One-click waveform sync (audio_sync.h): correlate every
    // audio-bearing clip's envelope against the selected/first clip
    // and apply matches as history-wrapped moves.
    void SyncAudioClips();
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
    void SetRightTab(int tab);                          // 0..9
    void ApplyProjectSettings(const ProjectSettings& settings);
    void AddGeneratedClip(SourceKind kind);
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
    void ApplyClipSpeed(const std::string& clip_id, double rate,
                        bool ripple);
    void AddAdjustmentClip();
    void RefreshCatalog();
    void NotifyProjectChanged();
    void SetStatus(const std::string& text, bool is_error = false);
    // Action dispatcher behind xer_ext.call()/projectInfo()/setStatus()
    // — JSON in/out, scoped surface for extension scripts.
    std::string DispatchExtensionAction(const std::string& action,
                                        const std::string& arg_json);
    MediaBounds BoundsForPath(const std::string& path) const;
    const CatalogEntry* FindCatalogEntry(const std::string& path) const;

    void* parent_hwnd_ = nullptr;
    std::string project_path_;
    Project project_;
    TimelineHistory history_;
    MediaCatalog catalog_;
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
    // Chrome rects cached by Layout() for the structural strips Render()
    // paints (status bar, right-panel tab strip, timeline divider).
    float status_bar_top_ = 0.0f;
    float timeline_top_ = 0.0f;
    XER::UI::Layout::Rect tab_strip_rect_{};
    // Active tab's bounds (cached by Layout) so Render() can draw the
    // accent underline indicator under it.
    XER::UI::Layout::Rect active_tab_rect_{};
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
        std::vector<std::pair<double, std::string>> markers;
        std::string detail;
        std::string bounce_path;   // audio-plugin bounce output (WAV)
    };
    std::mutex ai_result_mutex_;
    AiResult ai_result_;
    float ai_progress_shown_ = -1.0f;

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
    std::shared_ptr<BrowserPanelWidget> browser_panel_;
    std::shared_ptr<MarkersPanelWidget> markers_panel_;
    std::shared_ptr<LabPanel> lab_panel_;
    std::shared_ptr<TimelineWidget> timeline_;
    std::shared_ptr<ExportDialog> export_dialog_;
    std::shared_ptr<SettingsDialog> settings_dialog_;
    std::shared_ptr<HistoryPanelWidget> history_panel_;
    std::shared_ptr<MixerPanelWidget> mixer_panel_;
    std::shared_ptr<ShortcutsOverlay> shortcuts_overlay_;
    std::shared_ptr<SpeedDialog> speed_dialog_;
    std::shared_ptr<TitleEditor> title_editor_;
    std::shared_ptr<ExportQueuePanelWidget> export_queue_;
    std::shared_ptr<WaveformPanelWidget> waveform_panel_;
    std::shared_ptr<StatusToastWidget> status_toast_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_inspector_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_effects_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_markers_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_lab_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_history_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_mixer_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_queue_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_plugins_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_color_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> tab_browser_btn_;
    // 0 Insp, 1 FX, 2 Mkr, 3 Lab, 4 Hist, 5 Mix, 6 Queue, 7 Plugins,
    // 8 Color, 9 Web
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
};

}  // namespace XER::VideoEditor::Ui
