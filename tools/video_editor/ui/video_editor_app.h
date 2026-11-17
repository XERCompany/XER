// XER Studio video editor shell. Premiere/AE-style workspace built on
// XER_UI_Core: media bin + program monitor + inspector + timeline.
// The Project itself stays headless and every edit goes through
// TimelineHistory; preview and export consume snapshots.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "export_engine.h"
#include "media_catalog.h"
#include "project_autosave.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "playback/preview_engine.h"
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
#include "settings_dialog.h"
#include "shortcuts_overlay.h"
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
    void BuildTitleBar();
    void NewProject();
    void OpenProject();
    bool LoadProject(const std::string& utf8_path);
    bool SaveProject(bool choose_path = false);
    void ImportMedia();
    void AddSelectedToTimeline();
    void StartExport();          // opens the configured export dialog
    void BeginConfiguredExport();  // dialog "Export" -> engine start
    void AddTrack(TrackKind kind);
    void SelectClip(const std::string& clip_id);
    void Seek(double seconds);
    void TogglePlay();
    void ApplyClip(const Clip& clip);
    void MoveClip(const std::string& clip_id, double start);
    void TrimClip(const std::string& clip_id, TrimEdge edge,
                  double point);
    void SplitClip(const std::string& clip_id, double at_seconds);
    void DeleteClip(const std::string& clip_id);
    void AddTransitionAfter(const std::string& clip_id);
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
    void SetRightTab(int tab);                          // 0..3
    void ApplyProjectSettings(const ProjectSettings& settings);
    void AddGeneratedClip(SourceKind kind);
    void AddVst3FxToSelectedClip();
    void OpenVst3FxEditor();
    bool RunLabOperation(const LabPanel::RunParams& params,
                         const Lab::JobControl& job);
    void Undo();
    void Redo();
    void ToggleShortcutsOverlay();
    void ApplyWorkspace(const WorkspaceLayout& layout);
    void RefreshWaveformPanel();
    void RefreshExportQueue();
    void InitAutosave();
    void OpenSpeedDialog();
    void OpenTitleEditor();
    void ApplyClipSpeed(const std::string& clip_id, double rate,
                        bool ripple);
    void AddAdjustmentClip();
    void RefreshCatalog();
    void NotifyProjectChanged();
    void SetStatus(const std::string& text, bool is_error = false);
    MediaBounds BoundsForPath(const std::string& path) const;
    const CatalogEntry* FindCatalogEntry(const std::string& path) const;

    void* parent_hwnd_ = nullptr;
    std::string project_path_;
    Project project_;
    TimelineHistory history_;
    MediaCatalog catalog_;
    std::vector<CatalogEntry> catalog_entries_;
    std::string selected_media_path_;
    std::string selected_clip_id_;
    double playhead_ = 0.0;
    bool snap_enabled_ = true;
    bool left_panel_visible_ = true;
    bool right_panel_visible_ = true;
    bool children_attached_ = false;
    bool shortcuts_installed_ = false;

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

    std::shared_ptr<MediaBinWidget> media_bin_;
    std::shared_ptr<MonitorWidget> monitor_;
    std::shared_ptr<InspectorWidget> inspector_;
    std::shared_ptr<EffectsPanelWidget> effects_panel_;
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
    // 0 Insp, 1 FX, 2 Mkr, 3 Lab, 4 Hist, 5 Mix, 6 Queue
    int right_tab_ = 0;
    float master_gain_db_ = 0.0f;  // mixer master strip (model-less)
    double timeline_fraction_ = 0.32;  // workspace layout knob
    std::vector<ExportJobRow> export_jobs_;
    AutosaveManager autosave_;        // initialized once a path exists
    double autosave_poll_ = 0.0;      // Tick accumulator (seconds)
    ExportEngine export_engine_;

    // PreviewEngine drives playback frames + audio meters; the monitor
    // renders them through its external-playback path. Callbacks fire
    // on the engine worker — they only fill pending_* slots under the
    // mutex and the app Tick marshals them onto widgets.
    class EngineSink;
    std::unique_ptr<EngineSink> engine_sink_;
    // Platform audio output (WASAPI on Windows). Declared before the
    // engine so it outlives it — the engine only borrows the pointer.
    std::unique_ptr<IAudioSink> platform_audio_sink_;
    bool platform_audio_tried_ = false;
    PreviewEngine preview_engine_;
    bool preview_ready_ = false;
    bool preview_dirty_ = true;   // project changed since last Init
    std::mutex preview_mutex_;
    std::vector<std::uint8_t> pending_frame_;
    int pending_frame_w_ = 0, pending_frame_h_ = 0;
    bool has_pending_frame_ = false;
    float pending_peak_l_ = 0.0f, pending_peak_r_ = 0.0f;
    bool has_pending_peaks_ = false;
    double pending_playhead_ = -1.0;

    void EnsurePreviewEngine();
    void OnEngineFrame(const video::Frame& frame);
    void OnEnginePeaks(float l, float r);
    void OnEnginePlayhead(double t);

    // HWND-embedded VST3 plugin editor session (ui/vst3_editor.h).
    Vst3PluginEditor vst3_editor_;

    // Lab ops run on the panel's worker; the produced PNG is stashed
    // here and imported+added to the timeline by the next Tick.
    std::mutex lab_mutex_;
    std::string pending_lab_png_;
    bool has_pending_lab_ = false;
};

}  // namespace XER::VideoEditor::Ui
