#include "video_editor_app.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include "content/video/video_info.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "generated_source.h"
#include "lab/async_op.h"
#include "lab/object_ops.h"
#include "playback/audio_sink_factory.h"
#include "project_store.h"
#include "rendering/renderer.h"
#include "video/frame_exporter.h"
#include "theme/tokens.h"
#include "widgets/app_title_bar.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/widget_tree.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Path = XER::Helper::Path;
namespace Str = XER::Helper::String;
namespace Widgets = XER::UI::Widgets;

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

#ifdef _WIN32
std::wstring FilterSpec(const wchar_t* name, const wchar_t* pattern) {
    std::wstring filter = name;
    filter.push_back(L'\0');
    filter += pattern;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

std::wstring PickOpenFileW(HWND owner, const std::wstring& filter,
                           const wchar_t* title) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return file;
}

std::wstring PickSaveFileW(HWND owner, const std::wstring& filter,
                           const wchar_t* title, const wchar_t* extension,
                           const std::wstring& initial_name = L"") {
    wchar_t file[MAX_PATH] = L"";
    if (!initial_name.empty()) {
        wcscpy_s(file, initial_name.c_str());
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = extension;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    return file;
}
#endif

std::shared_ptr<Widgets::Button> MakeButton(
    const std::string& text, Widgets::ButtonStyle style,
    std::function<void()> action, const std::string& tooltip = {}) {
    auto button = std::make_shared<Widgets::Button>(text, style);
    if (!tooltip.empty()) button->SetTooltip(tooltip);
    button->SetOnClick(std::move(action));
    return button;
}

}  // namespace

// PreviewEngine callbacks fire on the engine worker — the sink only
// stashes them into the app's pending slots; Tick marshals to widgets.
class VideoEditorAppWidget::EngineSink final : public PreviewEngine::Sink {
public:
    explicit EngineSink(VideoEditorAppWidget* app) : app_(app) {}
    void OnVideoFrame(const video::Frame& frame) override {
        app_->OnEngineFrame(frame);
    }
    void OnAudioPeakLevels(float l, float r) override {
        app_->OnEnginePeaks(l, r);
    }
    void OnPlayhead(double t) override { app_->OnEnginePlayhead(t); }

private:
    VideoEditorAppWidget* app_;
};

VideoEditorAppWidget::VideoEditorAppWidget(void* parent_hwnd,
                                           std::string project_path)
    : parent_hwnd_(parent_hwnd), project_path_(std::move(project_path)),
      history_(project_) {
    EnsureChildren();
    std::string error;
    if (!catalog_.Open(&error)) {
        SetStatus("Media catalog unavailable: " + error, true);
    }
    RefreshCatalog();
    if (!project_path_.empty()) {
        LoadProject(project_path_);
    } else {
        NewProject();
    }
}

VideoEditorAppWidget::~VideoEditorAppWidget() {
    preview_engine_.Shutdown();
    export_engine_.Cancel();
    export_engine_.Wait();
}

void VideoEditorAppWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    BuildTitleBar();

    import_btn_ = MakeButton("Import", Widgets::ButtonStyle::Primary,
                             [this] { ImportMedia(); }, "Import media");
    add_btn_ = MakeButton("Add", Widgets::ButtonStyle::Secondary,
                          [this] { AddSelectedToTimeline(); },
                          "Add selected media at playhead");
    undo_btn_ = MakeButton("Undo", Widgets::ButtonStyle::Ghost,
                           [this] { Undo(); }, "Ctrl+Z");
    redo_btn_ = MakeButton("Redo", Widgets::ButtonStyle::Ghost,
                           [this] { Redo(); }, "Ctrl+Y");
    split_btn_ = MakeButton("Split", Widgets::ButtonStyle::Ghost,
                            [this] { SplitClip(selected_clip_id_, playhead_); },
                            "Split selected clip at playhead");
    delete_btn_ = MakeButton("Delete", Widgets::ButtonStyle::Ghost,
                             [this] { DeleteClip(selected_clip_id_); },
                             "Delete selected clip");
    snap_btn_ = MakeButton("Snap", Widgets::ButtonStyle::Secondary,
                           [this] {
                               snap_enabled_ = !snap_enabled_;
                               timeline_->SetSnapEnabled(snap_enabled_);
                               snap_btn_->SetStyle(
                                   snap_enabled_
                                       ? Widgets::ButtonStyle::Secondary
                                       : Widgets::ButtonStyle::Ghost);
                               SetStatus(snap_enabled_ ? "Snapping on"
                                                       : "Snapping off");
                           },
                           "Toggle timeline snapping (S)");
    play_btn_ = MakeButton("Play", Widgets::ButtonStyle::Secondary,
                           [this] { TogglePlay(); }, "Space");
    export_btn_ = MakeButton("Export", Widgets::ButtonStyle::Primary,
                             [this] { StartExport(); },
                             "Render timeline to MP4");
    status_label_ = std::make_shared<Widgets::Label>("Ready", true);
    zoom_label_ = std::make_shared<Widgets::Label>(
        "Wheel: zoom  Shift+Wheel: scroll", true);

    MediaBinWidget::Hooks media_hooks;
    media_hooks.on_import = [this] { ImportMedia(); };
    media_hooks.on_add_to_timeline = [this](const std::string&) {
        AddSelectedToTimeline();
    };
    media_hooks.on_select = [this](const std::string& path) {
        selected_media_path_ = path;
        SetStatus("Selected media: " + Basename(path));
    };
    media_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    media_bin_ =
        std::make_shared<MediaBinWidget>(parent_hwnd_, media_hooks);

    MonitorWidget::Hooks monitor_hooks;
    monitor_hooks.on_time_changed = [this](double t) {
        playhead_ = std::clamp(t, 0.0, project_.duration());
        timeline_->SetPlayhead(playhead_);
    };
    monitor_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    monitor_ = std::make_shared<MonitorWidget>(monitor_hooks);

    InspectorWidget::Hooks inspector_hooks;
    inspector_hooks.on_apply_clip = [this](const Clip& clip) {
        ApplyClip(clip);
    };
    inspector_hooks.on_delete_clip = [this](const std::string& id) {
        DeleteClip(id);
    };
    inspector_hooks.on_add_transition = [this](const std::string& id) {
        AddTransitionAfter(id);
    };
    inspector_hooks.on_set_keyframes =
        [this](const std::string& id, KeyProp prop,
               std::vector<Keyframe> keys) {
            SetClipKeyframesBy(id, prop, std::move(keys));
        };
    inspector_hooks.on_set_rate = [this](const std::string& id,
                                       double rate) {
        SetClipRateBy(id, rate);
    };
    inspector_hooks.on_seek = [this](double t) { Seek(t); };
    inspector_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    inspector_ = std::make_shared<InspectorWidget>(inspector_hooks);

    EffectsPanelWidget::Hooks effects_hooks;
    effects_hooks.on_add_effect =
        [this](const std::string& id, const ClipEffect& fx) {
            AddClipEffectBy(id, fx);
        };
    effects_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    effects_panel_ = std::make_shared<EffectsPanelWidget>(effects_hooks);

    MarkersPanelWidget::Hooks markers_hooks;
    markers_hooks.on_seek = [this](double t) { Seek(t); };
    markers_hooks.on_add_marker =
        [this](double t, const std::string& name) {
            AddMarkerAt(t, name);
        };
    markers_hooks.on_remove_marker = [this](double t) {
        RemoveMarkerAt(t);
    };
    markers_hooks.on_rename_marker =
        [this](double t, const std::string& name) {
            RenameMarkerAt(t, name);
        };
    markers_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    markers_panel_ =
        std::make_shared<MarkersPanelWidget>(markers_hooks);

    LabPanel::Hooks lab_hooks;
    lab_hooks.on_run =
        [this](const LabPanel::RunParams& params,
               const Lab::JobControl& job) {
            return RunLabOperation(params, job);
        };
    lab_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    lab_panel_ = std::make_shared<LabPanel>(lab_hooks);

    HistoryPanelWidget::Hooks history_hooks;
    history_hooks.on_undo_steps = [this](int steps) {
        int done = 0;
        while (done < steps && history_.Undo()) ++done;
        if (done > 0) {
            NotifyProjectChanged();
            SetStatus("Undo x" + std::to_string(done));
        }
    };
    history_hooks.on_redo_steps = [this](int steps) {
        int done = 0;
        while (done < steps && history_.Redo()) ++done;
        if (done > 0) {
            NotifyProjectChanged();
            SetStatus("Redo x" + std::to_string(done));
        }
    };
    history_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    history_panel_ =
        std::make_shared<HistoryPanelWidget>(history_hooks);

    MixerPanelWidget::Hooks mixer_hooks;
    mixer_hooks.on_track_gain = [this](int track, float db) {
        if (track < 0 ||
            track >= static_cast<int>(project_.tracks.size())) return;
        // Continuous fader writes — invalidate the engine snapshot but
        // skip the full Notify rebuild while the drag is live.
        project_.tracks[track].gain_db = db;
        preview_dirty_ = true;
        Invalidate();
    };
    mixer_hooks.on_track_pan = [](int, float) {
        // The model has no per-track pan yet; the panel keeps its own
        // pan map until a model field lands.
    };
    mixer_hooks.on_track_mute = [this](int track, bool on) {
        SetTrackFlag(track, 'm', on);
    };
    mixer_hooks.on_master_gain = [this](float db) {
        master_gain_db_ = db;
        preview_dirty_ = true;
    };
    mixer_panel_ = std::make_shared<MixerPanelWidget>(mixer_hooks);

    ShortcutsOverlay::Hooks shortcuts_hooks;
    shortcuts_hooks.on_close = [this] { HideOverlay(); };
    shortcuts_overlay_ =
        std::make_shared<ShortcutsOverlay>(shortcuts_hooks);

    SpeedDialog::Hooks speed_hooks;
    speed_hooks.on_apply =
        [this](double rate, bool ripple, bool /*reverse*/) {
            HideOverlay();
            ApplyClipSpeed(selected_clip_id_, rate, ripple);
        };
    speed_hooks.on_cancel = [this] { HideOverlay(); };
    speed_dialog_ = std::make_shared<SpeedDialog>(speed_hooks);

    TitleEditor::Hooks title_hooks;
    title_hooks.on_apply = [this](const TitleStyle& style) {
        HideOverlay();
        Clip* clip = FindClip(project_, selected_clip_id_);
        if (clip == nullptr || clip->source.kind != SourceKind::Text) {
            return;
        }
        auto hex = [](const std::array<std::uint8_t, 4>& rgba) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
                          rgba[0], rgba[1], rgba[2], rgba[3]);
            return std::string(buf);
        };
        static const char* kAlign[] = {"left", "center", "right"};
        Clip updated = *clip;
        updated.source.param =
            style.text + " | size=" + std::to_string(style.size_px) +
            " color=" + hex(style.color) +
            " outline=" + std::to_string(style.outline_px) +
            " outline_color=" + hex(style.outline_color) +
            " align=" + kAlign[std::clamp(style.align, 0, 2)];
        std::string error;
        if (!history_.ReplaceClip(updated.id, updated,
                                  BoundsForPath(updated.source_path),
                                  &error)) {
            SetStatus("Title update failed: " + error, true);
            return;
        }
        NotifyProjectChanged();
        SetStatus("Title updated");
    };
    title_hooks.on_cancel = [this] { HideOverlay(); };
    title_editor_ = std::make_shared<TitleEditor>(title_hooks);

    ExportQueuePanelWidget::Hooks queue_hooks;
    queue_hooks.on_cancel = [this](int) { export_engine_.Cancel(); };
    queue_hooks.on_reveal = [this](int job) {
        if (job >= 0 && job < static_cast<int>(export_jobs_.size())) {
            SetStatus(export_jobs_[job].label);
        }
    };
    queue_hooks.on_retry = [this](int) { StartExport(); };
    queue_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    export_queue_ =
        std::make_shared<ExportQueuePanelWidget>(queue_hooks);
    waveform_panel_ = std::make_shared<WaveformPanelWidget>();
    status_toast_ = std::make_shared<StatusToastWidget>();

    auto tab = [this](const char* text, int index,
                      const std::string& tooltip) {
        return MakeButton(text, right_tab_ == index
                                    ? Widgets::ButtonStyle::Secondary
                                    : Widgets::ButtonStyle::Ghost,
                          [this, index] { SetRightTab(index); }, tooltip);
    };
    tab_inspector_btn_ = tab("Insp", 0, "Inspector — clip properties");
    tab_effects_btn_ = tab("FX", 1, "Effects — effect library");
    tab_markers_btn_ = tab("Mkr", 2, "Markers — timeline markers");
    tab_lab_btn_ = tab("Lab", 3, "Lab — object tools (MKL)");
    tab_history_btn_ = tab("Hist", 4, "History — undo/redo");
    tab_mixer_btn_ = tab("Mix", 5, "Mixer — audio tracks");
    tab_queue_btn_ = tab("Q", 6, "Queue — export jobs");

    TimelineWidget::Hooks timeline_hooks;
    timeline_hooks.on_select_clip = [this](const std::string& id) {
        SelectClip(id);
    };
    timeline_hooks.on_seek = [this](double t) { Seek(t); };
    timeline_hooks.on_move_clip = [this](const std::string& id, double t) {
        MoveClip(id, t);
    };
    timeline_hooks.on_trim_clip =
        [this](const std::string& id, TrimEdge edge, double point) {
            TrimClip(id, edge, point);
        };
    timeline_hooks.on_ripple_trim =
        [this](const std::string& id, TrimEdge edge, double point) {
            RippleTrimClip(id, edge, point);
        };
    timeline_hooks.on_delete_clip = [this](const std::string& id) {
        DeleteClip(id);
    };
    timeline_hooks.on_ripple_delete = [this](const std::string& id) {
        RippleDeleteClipById(id);
    };
    timeline_hooks.on_split_clip = [this](const std::string& id, double t) {
        SplitClip(id, t);
    };
    timeline_hooks.on_rolling_edit =
        [this](const std::string& from, const std::string& to,
               double delta) { RollingEditClips(from, to, delta); };
    timeline_hooks.on_slip_clip =
        [this](const std::string& id, double delta) {
            SlipClipBy(id, delta);
        };
    timeline_hooks.on_slide_clip =
        [this](const std::string& id, double delta) {
            SlideClipBy(id, delta);
        };
    timeline_hooks.on_rate_stretch =
        [this](const std::string& id, double rate) {
            SetClipRateBy(id, rate);
        };
    timeline_hooks.on_add_marker =
        [this](double t, const std::string& name) {
            AddMarkerAt(t, name);
        };
    timeline_hooks.on_remove_marker = [this](double t) {
        RemoveMarkerAt(t);
    };
    timeline_hooks.on_set_work_area = [this](double in, double out) {
        SetWorkAreaRange(in, out);
    };
    timeline_hooks.on_track_mute = [this](int track, bool on) {
        SetTrackFlag(track, 'm', on);
    };
    timeline_hooks.on_track_hidden = [this](int track, bool on) {
        SetTrackFlag(track, 'h', on);
    };
    timeline_hooks.on_track_locked = [this](int track, bool on) {
        SetTrackFlag(track, 'l', on);
    };
    timeline_hooks.get_audio_peaks =
        [this](const std::string& path, AudioPeaks* out) {
            return catalog_.GetAudioPeaks(path, out);
        };
    timeline_hooks.get_poster =
        [](const std::string& path, double src_t, video::Frame* frame) {
            return MediaCatalog::GetPosterAt(path, src_t, frame);
        };
    timeline_hooks.get_media_bounds =
        [this](const std::string& path, MediaBounds* out) {
            *out = BoundsForPath(path);
            return true;
        };
    timeline_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    timeline_ = std::make_shared<TimelineWidget>(timeline_hooks);

    ExportDialog::Hooks export_hooks;
    export_hooks.on_cancel_or_close = [this] {
        if (export_engine_.IsRunning()) {
            export_engine_.Cancel();
            SetStatus("Cancelling export…");
        } else {
            HideOverlay();
        }
    };
    export_hooks.on_export_start = [this] { BeginConfiguredExport(); };
    export_dialog_ = std::make_shared<ExportDialog>(export_hooks);

    SettingsDialog::Hooks settings_hooks;
    settings_hooks.on_apply = [this](const ProjectSettings& settings) {
        ApplyProjectSettings(settings);
        HideOverlay();
    };
    settings_hooks.on_cancel = [this] { HideOverlay(); };
    settings_dialog_ =
        std::make_shared<SettingsDialog>(settings_hooks);

    AddChild(title_bar_);
    AddChild(import_btn_);
    AddChild(add_btn_);
    AddChild(undo_btn_);
    AddChild(redo_btn_);
    AddChild(split_btn_);
    AddChild(delete_btn_);
    AddChild(snap_btn_);
    AddChild(play_btn_);
    AddChild(export_btn_);
    AddChild(status_label_);
    AddChild(zoom_label_);
    AddChild(media_bin_);
    AddChild(monitor_);
    AddChild(inspector_);
    AddChild(effects_panel_);
    AddChild(markers_panel_);
    AddChild(lab_panel_);
    AddChild(history_panel_);
    AddChild(mixer_panel_);
    AddChild(export_queue_);
    AddChild(waveform_panel_);
    AddChild(status_toast_);
    AddChild(tab_inspector_btn_);
    AddChild(tab_effects_btn_);
    AddChild(tab_markers_btn_);
    AddChild(tab_lab_btn_);
    AddChild(tab_history_btn_);
    AddChild(tab_mixer_btn_);
    AddChild(tab_queue_btn_);
    AddChild(timeline_);
}

void VideoEditorAppWidget::InstallGlobalShortcuts() {
    if (shortcuts_installed_ || GetTree() == nullptr) return;
    shortcuts_installed_ = true;
    GetTree()->SetKeyHandler([this](const Widgets::Event& event) {
        return HandleEvent(event);
    });
}

void VideoEditorAppWidget::BuildTitleBar() {
    using Menu = Widgets::AppTitleBar::Menu;
    using MenuItem = Widgets::AppTitleBar::MenuItem;
    title_bar_ = std::make_shared<Widgets::AppTitleBar>("XER Studio");
    title_bar_->SetWorkspaceLabel("Video Editor");
    title_bar_->SetCenterCommand("Untitled.xerproj", nullptr, false);

    auto item = [](std::string label, std::string shortcut,
                   std::function<void()> action, bool enabled = true,
                   bool checked = false, bool danger = false,
                   bool separator = false) {
        MenuItem menu_item;
        menu_item.label = std::move(label);
        menu_item.shortcut = std::move(shortcut);
        menu_item.action = std::move(action);
        menu_item.enabled = enabled;
        menu_item.checked = checked;
        menu_item.danger = danger;
        menu_item.separator_before = separator;
        return menu_item;
    };

    Menu file;
    file.label = "File";
    file.items = {
        item("New Project", "Ctrl+N", [this] { NewProject(); }),
        item("Open Project…", "Ctrl+O", [this] { OpenProject(); }),
        item("Save", "Ctrl+S", [this] { SaveProject(false); }),
        item("Save As…", "Ctrl+Shift+S",
             [this] { SaveProject(true); }),
        item("Import Media…", "", [this] { ImportMedia(); }, true, false,
             false, true),
        item("Project Settings…", "", [this] {
            ProjectSettings settings;
            settings.w = project_.width;
            settings.h = project_.height;
            settings.fps_num = project_.fps_num;
            settings.fps_den = project_.fps_den;
            settings.bg = project_.bg_color;
            settings_dialog_->SetProjectName(
                project_path_.empty() ? "Untitled.xerproj"
                                      : Basename(project_path_));
            settings_dialog_->SetSettings(settings);
            ShowOverlay(settings_dialog_);
        }, true, false, false, true),
        item("Export MP4…", "Ctrl+P", [this] { StartExport(); }, true,
             false, false, true),
    };
    Menu edit;
    edit.label = "Edit";
    edit.items = {
        item("Undo", "Ctrl+Z", [this] { Undo(); }),
        item("Redo", "Ctrl+Y", [this] { Redo(); }),
        item("Delete Clip", "Del",
             [this] { DeleteClip(selected_clip_id_); }, true, false,
             true, true),
        item("Ripple Delete", "Alt+Del",
             [this] { RippleDeleteClipById(selected_clip_id_); }, true,
             false, true),
        item("Add VST3 Effect…", "",
             [this] { AddVst3FxToSelectedClip(); }, true, false, false,
             true),
        item("Open Plugin Editor", "",
             [this] { OpenVst3FxEditor(); }),
    };
    Menu timeline;
    timeline.label = "Timeline";
    const Clip* menu_sel_clip = FindClip(project_, selected_clip_id_);
    timeline.items = {
        item("Add Video Track", "", [this] { AddTrack(TrackKind::Video); }),
        item("Add Audio Track", "", [this] { AddTrack(TrackKind::Audio); }),
        item("New Color Matte", "",
             [this] { AddGeneratedClip(SourceKind::ColorMatte); }, true,
             false, false, true),
        item("New Color Bars", "",
             [this] { AddGeneratedClip(SourceKind::ColorBars); }),
        item("New Title", "",
             [this] { AddGeneratedClip(SourceKind::Text); }),
        item("New Adjustment Layer", "",
             [this] { AddAdjustmentClip(); }),
        item("Edit Title…", "", [this] { OpenTitleEditor(); },
             menu_sel_clip != nullptr &&
                 menu_sel_clip->source.kind == SourceKind::Text),
        item("Speed/Duration…", "", [this] { OpenSpeedDialog(); },
             menu_sel_clip != nullptr),
        item("Add Marker", "M",
             [this] { AddMarkerAt(playhead_, ""); }, true, false, false,
             true),
        item("Split at Playhead", "C",
             [this] { SplitClip(selected_clip_id_, playhead_); }, true,
             false, false, true),
        item("Toggle Snapping", "S", [this] {
            snap_enabled_ = !snap_enabled_;
            timeline_->SetSnapEnabled(snap_enabled_);
            snap_btn_->SetStyle(snap_enabled_
                                    ? Widgets::ButtonStyle::Secondary
                                    : Widgets::ButtonStyle::Ghost);
            SetStatus(snap_enabled_ ? "Snapping on" : "Snapping off");
        }, true, snap_enabled_),
    };
    Menu view;
    view.label = "View";
    view.items = {
        item("Media Bin", "", [this] {
            left_panel_visible_ = !left_panel_visible_;
            RequestLayout();
        }, true, left_panel_visible_),
        item("Right Panel", "", [this] {
            right_panel_visible_ = !right_panel_visible_;
            RequestLayout();
        }, true, right_panel_visible_),
        item("Inspector", "", [this] { SetRightTab(0); }, true,
             right_tab_ == 0, false, true),
        item("Effects", "", [this] { SetRightTab(1); }, true,
             right_tab_ == 1),
        item("Markers", "", [this] { SetRightTab(2); }, true,
             right_tab_ == 2),
        item("Object Lab", "", [this] { SetRightTab(3); }, true,
             right_tab_ == 3),
        item("History", "", [this] { SetRightTab(4); }, true,
             right_tab_ == 4),
        item("Audio Mixer", "", [this] { SetRightTab(5); }, true,
             right_tab_ == 5),
        item("Keyboard Shortcuts", "?", [this] {
            ToggleShortcutsOverlay();
        }),
    };
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        view.items.push_back(
            item("Workspace: " + ws.name, "",
                 [this, ws] { ApplyWorkspace(ws); }));
    }
    title_bar_->SetMenus({file, edit, timeline, view});
    title_bar_->SetActionPill("Export", [this] { StartExport(); }, true);
    title_bar_->SetLeftPanelToggle(true, left_panel_visible_, [this] {
        left_panel_visible_ = !left_panel_visible_;
        RequestLayout();
    });
    title_bar_->SetRightPanelToggle(true, right_panel_visible_, [this] {
        right_panel_visible_ = !right_panel_visible_;
        RequestLayout();
    });
}

void VideoEditorAppWidget::NewProject() {
    if (export_engine_.IsRunning()) {
        SetStatus("Wait for export to finish", true);
        return;
    }
    history_.Clear();
    project_ = Project{};
    project_.width = 1920;
    project_.height = 1080;
    project_.fps_num = 30;
    project_.fps_den = 1;
    project_.bg_color = {0, 0, 0, 255};
    project_.tracks.push_back(Track{TrackKind::Video, "Video 1"});
    project_.tracks.push_back(Track{TrackKind::Audio, "Audio 1"});
    project_path_.clear();
    selected_clip_id_.clear();
    playhead_ = 0.0;
    NotifyProjectChanged();
    SetStatus("New project");
}

bool VideoEditorAppWidget::LoadProject(const std::string& utf8_path) {
    std::string error;
    history_.Clear();
    if (!ProjectStore::Load(Path::NormalizeUtf8Path(utf8_path), &project_,
                            &error)) {
        SetStatus("Open failed: " + error, true);
        return false;
    }
    project_path_ = Path::NormalizeUtf8Path(utf8_path);
    InitAutosave();
    selected_clip_id_.clear();
    playhead_ = 0.0;
    NotifyProjectChanged();
    SetStatus("Opened " + Basename(project_path_));
    return true;
}

void VideoEditorAppWidget::OpenProject() {
#ifdef _WIN32
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"XER project", L"*.xerproj"), L"Open project");
    if (!picked.empty()) LoadProject(Str::WideToUtf8(picked));
#endif
}

bool VideoEditorAppWidget::SaveProject(bool choose_path) {
    if (project_path_.empty() || choose_path) {
#ifdef _WIN32
        const auto picked = PickSaveFileW(
            static_cast<HWND>(parent_hwnd_),
            FilterSpec(L"XER project", L"*.xerproj"), L"Save project",
            L"xerproj", L"untitled.xerproj");
        if (picked.empty()) return false;
        project_path_ = Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
#else
        SetStatus("Save dialog is Windows-only", true);
        return false;
#endif
    }
    std::string error;
    if (!ProjectStore::Save(project_, project_path_, &error)) {
        SetStatus("Save failed: " + error, true);
        return false;
    }
    SetStatus("Saved " + Basename(project_path_));
    InitAutosave();   // bind to a first-saved (or renamed) path
    autosave_.ClearDirty();
    if (title_bar_) title_bar_->SetCenterCommandValue(Basename(project_path_));
    return true;
}

void VideoEditorAppWidget::ImportMedia() {
#ifdef _WIN32
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"Media", L"*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.m4v;*.ts;*.wav;*.mp3;*.aac;*.flac;*.ogg;*.png;*.jpg;*.jpeg"),
        L"Import media");
    if (picked.empty()) return;
    const std::string path =
        Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
    std::int64_t id = 0;
    std::string error;
    if (!catalog_.Open(&error) ||
        !catalog_.Import(path, &id, true, &error)) {
        SetStatus("Import failed: " + error, true);
        return;
    }
    selected_media_path_ = path;
    RefreshCatalog();
    SetStatus("Imported " + Basename(path));
#endif
}

void VideoEditorAppWidget::AddSelectedToTimeline() {
    const CatalogEntry* entry = FindCatalogEntry(selected_media_path_);
    if (entry == nullptr) {
        SetStatus("Select media in the bin first", true);
        return;
    }
    const TrackKind wanted =
        entry->HasVideo() ? TrackKind::Video : TrackKind::Audio;
    int track_index = -1;
    for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
        if (project_.tracks[i].kind == wanted) {
            track_index = static_cast<int>(i);
            break;
        }
    }
    if (track_index < 0) {
        track_index = history_.AddTrack(
            wanted, wanted == TrackKind::Video ? "Video" : "Audio");
        if (track_index < 0) {
            SetStatus("Unable to add track", true);
            return;
        }
    }

    Clip clip;
    clip.id = NewClipId();
    clip.source_path = entry->path;
    clip.track = track_index;
    clip.timeline_start = snap_enabled_
        ? SnapTime(project_, track_index, playhead_, 0.1, playhead_)
        : playhead_;
    clip.in_point = 0.0;
    clip.duration = entry->duration_seconds > 0.0
        ? entry->duration_seconds
        : 5.0;
    if (entry->HasVideo()) {
        clip.transform = FitTransformToCanvas(entry->width, entry->height,
                                              project_.width,
                                              project_.height);
    }
    MediaBounds bounds;
    bounds.duration_seconds = entry->duration_seconds;
    bounds.has_video = entry->HasVideo();
    std::string error;
    std::string clip_id;
    if (!history_.AddClip(std::move(clip), bounds, &clip_id, &error)) {
        SetStatus("Add clip failed: " + error, true);
        return;
    }
    SelectClip(clip_id);
    NotifyProjectChanged();
    SetStatus("Added " + Basename(entry->path));
}

void VideoEditorAppWidget::StartExport() {
    if (export_engine_.IsRunning()) {
        SetStatus("Export already running", true);
        return;
    }
    if (project_.duration() <= 0.0) {
        SetStatus("Timeline is empty", true);
        return;
    }
    // Two-phase flow: the dialog first shows its range section for
    // configuration; the action button then calls on_export_start.
    export_dialog_->SetProject(&project_);
    export_dialog_->Reset();
    ShowOverlay(export_dialog_);
}

void VideoEditorAppWidget::BeginConfiguredExport() {
    if (export_engine_.IsRunning()) return;
#ifdef _WIN32
    const auto picked = PickSaveFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"MP4 video", L"*.mp4"), L"Export video", L"mp4",
        L"timeline.mp4");
    if (picked.empty()) return;   // stay in the configured dialog
    const std::string output =
        Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
#else
    SetStatus("Export dialog is Windows-only", true);
    return;
#endif
    // GetExportOptions merges the selected preset (codec/bitrate/
    // scale) with the configured range mode.
    ExportOptions options = export_dialog_->GetExportOptions();
    options.output_path = output;
    std::string error;
    if (!export_engine_.Start(project_, options, {}, &error)) {
        SetStatus("Export failed: " + error, true);
        return;
    }
    export_dialog_->SetProgress(export_engine_.Progress());
    // Track the job in the export queue panel (cap the history at 8).
    ExportJobRow job;
    job.label = Basename(output);
    job.detail = options.video_codec + " " +
                 std::to_string(options.video_bit_rate / 1000) +
                 " kbps · " + options.audio_codec;
    job.progress = 0.0f;
    job.state = static_cast<int>(ExportJobState::Running);
    export_jobs_.push_back(std::move(job));
    if (export_jobs_.size() > 8) export_jobs_.erase(export_jobs_.begin());
    RefreshExportQueue();
    SetStatus("Exporting " + Basename(output));
}

void VideoEditorAppWidget::AddTrack(TrackKind kind) {
    const int index = history_.AddTrack(
        kind, kind == TrackKind::Video ? "Video" : "Audio");
    if (index < 0) {
        SetStatus("Unable to add track", true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added track " + std::to_string(index + 1));
}

void VideoEditorAppWidget::SelectClip(const std::string& clip_id) {
    selected_clip_id_ = clip_id;
    timeline_->SetSelectedClip(clip_id);
    inspector_->SetClip(FindClip(project_, clip_id));
    SetStatus(clip_id.empty() ? "Selection cleared"
                              : "Selected " + clip_id);
}

void VideoEditorAppWidget::Seek(double seconds) {
    playhead_ = std::clamp(seconds, 0.0, project_.duration());
    timeline_->SetPlayhead(playhead_);
    inspector_->SetPlayhead(playhead_);
    if (monitor_->IsPlaying() && preview_ready_) {
        // Rebase the engine clock; frames keep streaming externally.
        preview_engine_.Seek(playhead_);
        monitor_->SetTime(playhead_, false);
    } else {
        monitor_->SetPlaying(false);
        monitor_->SetExternalPlayback(false);
        monitor_->SetTime(playhead_, true);
        play_btn_->SetText("Play");
    }
    if (markers_panel_) markers_panel_->SetPlayhead(playhead_);
    if (waveform_panel_ && waveform_panel_->IsVisible()) {
        waveform_panel_->SetPlayhead(playhead_);
    }
}

void VideoEditorAppWidget::TogglePlay() {
    const bool next = !monitor_->IsPlaying();
    if (next) {
        EnsurePreviewEngine();
        if (!preview_ready_) {
            SetStatus("Preview engine unavailable", true);
            return;
        }
        if (playhead_ >= project_.duration() - 1e-6) playhead_ = 0.0;
        preview_engine_.Seek(playhead_);
        // Loop honors the monitor's explicit range, else the project's
        // work area; the engine resolves in>=out as "no loop".
        double loop_in = 0.0, loop_out = 0.0;
        if (monitor_->Loop()) {
            auto [li, lo] = monitor_->LoopRange();
            loop_in = li;
            loop_out = lo;
            if (loop_out <= loop_in) {
                loop_in = project_.work_in;
                loop_out = project_.work_out;
            }
        }
        preview_engine_.SetLoop(loop_in, loop_out);
        preview_engine_.SetPlaying(true);
        monitor_->SetExternalPlayback(true);
        monitor_->SetPlaying(true);
    } else {
        preview_engine_.SetPlaying(false);
        monitor_->SetPlaying(false);
        monitor_->SetExternalPlayback(false);
        monitor_->SetTime(playhead_, true);  // still at the stop point
    }
    play_btn_->SetText(next ? "Pause" : "Play");
}

void VideoEditorAppWidget::EnsurePreviewEngine() {
    if (preview_ready_ && !preview_dirty_) return;
    preview_engine_.Shutdown();
    preview_ready_ = false;
    if (!engine_sink_) {
        engine_sink_ = std::make_unique<EngineSink>(this);
    }
    if (!platform_audio_tried_) {
        platform_audio_tried_ = true;
        std::string sink_error;
        platform_audio_sink_ = MakePlatformAudioSink(&sink_error);
        if (!platform_audio_sink_ && !sink_error.empty()) {
            SetStatus("Audio preview disabled: " + sink_error);
        }
    }
    PreviewOptions options;
    options.audio_sink = platform_audio_sink_.get();
    std::string error;
    preview_ready_ = preview_engine_.Init(project_, options,
                                        engine_sink_.get(), &error);
    preview_dirty_ = false;
    if (!preview_ready_) {
        SetStatus("Preview init failed: " + error, true);
    }
}

void VideoEditorAppWidget::OnEngineFrame(const video::Frame& frame) {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    pending_frame_.assign(frame.Data(), frame.Data() + frame.DataSize());
    pending_frame_w_ = frame.Width();
    pending_frame_h_ = frame.Height();
    has_pending_frame_ = true;
}

void VideoEditorAppWidget::OnEnginePeaks(float l, float r) {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    pending_peak_l_ = l;
    pending_peak_r_ = r;
    has_pending_peaks_ = true;
}

void VideoEditorAppWidget::OnEnginePlayhead(double t) {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    pending_playhead_ = t;
}

void VideoEditorAppWidget::ApplyClip(const Clip& clip) {
    if (clip.id.empty()) return;
    std::string error;
    if (!history_.ReplaceClip(clip.id, clip, BoundsForPath(clip.source_path),
                              &error)) {
        SetStatus("Apply failed: " + error, true);
        inspector_->SetClip(FindClip(project_, clip.id));
        return;
    }
    NotifyProjectChanged();
    SetStatus("Clip updated");
}

void VideoEditorAppWidget::MoveClip(const std::string& clip_id,
                                    double start) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.MoveClip(clip_id, start,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Move failed: " + error, true);
    } else {
        SetStatus("Moved clip");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::TrimClip(const std::string& clip_id,
                                    TrimEdge edge, double point) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.TrimClip(clip_id, edge, point,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Trim failed: " + error, true);
    } else {
        SetStatus("Trimmed clip");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SplitClip(const std::string& clip_id,
                                     double at_seconds) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to split", true);
        return;
    }
    std::string new_id;
    std::string error;
    if (!history_.SplitClip(clip_id, at_seconds, &new_id, &error)) {
        SetStatus("Split failed: " + error, true);
        return;
    }
    SelectClip(new_id);
    NotifyProjectChanged();
    SetStatus("Split clip");
}

void VideoEditorAppWidget::DeleteClip(const std::string& clip_id) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to delete", true);
        return;
    }
    if (!history_.RemoveClip(clip_id)) {
        SetStatus("Delete failed", true);
        return;
    }
    selected_clip_id_.clear();
    NotifyProjectChanged();
    SetStatus("Deleted clip");
}

void VideoEditorAppWidget::AddTransitionAfter(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    const Clip* next = nullptr;
    for (const Clip& candidate : project_.clips) {
        if (candidate.track != clip->track ||
            candidate.timeline_start < ClipEndSeconds(*clip)) {
            continue;
        }
        if (next == nullptr ||
            candidate.timeline_start < next->timeline_start) {
            next = &candidate;
        }
    }
    if (next == nullptr) {
        SetStatus("No adjacent clip for transition", true);
        return;
    }
    std::string error;
    if (!history_.AddTransition(clip->id, next->id,
                                Transition::Type::Crossfade, 1.0,
                                &error)) {
        SetStatus("Transition failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added crossfade");
}

void VideoEditorAppWidget::RippleDeleteClipById(
    const std::string& clip_id) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to ripple-delete", true);
        return;
    }
    std::string error;
    if (!history_.RippleDeleteClip(clip_id, &error)) {
        SetStatus("Ripple delete failed: " + error, true);
        return;
    }
    if (selected_clip_id_ == clip_id) selected_clip_id_.clear();
    NotifyProjectChanged();
    SetStatus("Ripple deleted clip");
}

void VideoEditorAppWidget::RippleTrimClip(const std::string& clip_id,
                                          TrimEdge edge, double point) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    const int track = clip->track;
    const double old_start = clip->timeline_start;
    const double old_end = ClipEndSeconds(*clip);
    std::string error;
    history_.BeginMacro("Ripple trim");
    bool ok = history_.TrimClip(clip_id, edge, point,
                                BoundsForPath(clip->source_path), &error);
    if (ok) {
        // Freed span on this track, if any: head trims move the
        // clip's start right, tail trims pull its end left. Growth in
        // either direction needs no gap close (TrimClip already
        // rejected growth into occupied space).
        const Clip* after = FindClip(project_, clip_id);
        double gap = 0.0;
        double pivot = 0.0;   // followers start at/after this time
        if (after != nullptr && edge == TrimEdge::In) {
            gap = after->timeline_start - old_start;
            pivot = after->timeline_start;
        } else if (after != nullptr) {
            gap = old_end - ClipEndSeconds(*after);
            pivot = old_end;
        }
        if (gap > 1e-9) {
            std::vector<const Clip*> followers;
            for (const Clip& c : project_.clips) {
                if (c.track != track) continue;
                const bool self_moved =
                    edge == TrimEdge::In && c.id == clip_id;
                if (self_moved ||
                    (c.id != clip_id &&
                     c.timeline_start >= pivot - 1e-9)) {
                    followers.push_back(&c);
                }
            }
            std::sort(followers.begin(), followers.end(),
                      [](const Clip* a, const Clip* b) {
                          return a->timeline_start < b->timeline_start;
                      });
            // Move leftmost-first so each clip lands in freed space.
            for (const Clip* f : followers) {
                if (!history_.MoveClip(
                        f->id, f->timeline_start - gap,
                        BoundsForPath(f->source_path), &error)) {
                    ok = false;
                    break;
                }
            }
        }
    }
    history_.EndMacro();
    if (!ok) {
        SetStatus("Ripple trim failed: " + error, true);
    } else {
        SetStatus("Ripple trim");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::RollingEditClips(const std::string& from_id,
                                            const std::string& to_id,
                                            double delta) {
    const Clip* from = FindClip(project_, from_id);
    const Clip* to = FindClip(project_, to_id);
    if (from == nullptr || to == nullptr) return;
    std::string error;
    if (!history_.RollingEdit(from_id, to_id, delta,
                              BoundsForPath(from->source_path),
                              BoundsForPath(to->source_path), &error)) {
        SetStatus("Rolling edit failed: " + error, true);
    } else {
        SetStatus("Rolling edit");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SlipClipBy(const std::string& clip_id,
                                      double delta_src) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SlipClip(clip_id, delta_src,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Slip failed: " + error, true);
    } else {
        SetStatus("Slipped clip");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SlideClipBy(const std::string& clip_id,
                                       double delta_tl) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SlideClip(clip_id, delta_tl,
                            BoundsForPath(clip->source_path), &error)) {
        SetStatus("Slide failed: " + error, true);
    } else {
        SetStatus("Slid clip");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SetClipRateBy(const std::string& clip_id,
                                         double rate) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SetClipRate(clip_id, rate,
                              BoundsForPath(clip->source_path), &error)) {
        SetStatus("Rate change failed: " + error, true);
    } else {
        SetStatus("Rate " + std::to_string(rate));
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::OpenSpeedDialog() {
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr) {
        SetStatus("Select a clip first", true);
        return;
    }
    const MediaBounds bounds = BoundsForPath(clip->source_path);
    speed_dialog_->SetClip(clip->in_point, clip->duration,
                           clip->playback_rate,
                           bounds.duration_seconds);
    ShowOverlay(speed_dialog_);
}

void VideoEditorAppWidget::ApplyClipSpeed(const std::string& clip_id,
                                        double rate, bool ripple) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    const int track = clip->track;
    const double old_end = ClipEndSeconds(*clip);
    std::string error;
    history_.BeginMacro("Speed/Duration");
    bool ok = history_.SetClipRate(
        clip_id, rate, BoundsForPath(clip->source_path), &error);
    if (ok && ripple) {
        const Clip* after = FindClip(project_, clip_id);
        const double delta =
            after != nullptr ? ClipEndSeconds(*after) - old_end : 0.0;
        if (std::abs(delta) > 1e-9) {
            std::vector<const Clip*> followers;
            for (const Clip& c : project_.clips) {
                if (c.track == track && c.id != clip_id &&
                    c.timeline_start >= old_end - 1e-9) {
                    followers.push_back(&c);
                }
            }
            // Grow: move rightmost-first into freed space; shrink:
            // leftmost-first (mirrors the ripple-trim walk).
            std::sort(followers.begin(), followers.end(),
                      [delta](const Clip* a, const Clip* b) {
                          return delta > 0.0
                                     ? a->timeline_start > b->timeline_start
                                     : a->timeline_start < b->timeline_start;
                      });
            for (const Clip* f : followers) {
                if (!history_.MoveClip(
                        f->id, f->timeline_start + delta,
                        BoundsForPath(f->source_path), &error)) {
                    ok = false;
                    break;
                }
            }
        }
    }
    history_.EndMacro();
    SetStatus(ok ? "Speed applied"
                 : "Speed change failed: " + error, !ok);
    NotifyProjectChanged();
}

void VideoEditorAppWidget::OpenTitleEditor() {
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr || clip->source.kind != SourceKind::Text) {
        SetStatus("Select a title clip first", true);
        return;
    }
    const ClipTextStyle parsed =
        ParseClipTextStyle(clip->source.param);
    TitleStyle style;
    style.text = parsed.text;
    style.size_px = parsed.size_px > 0 ? parsed.size_px : 64;
    // ClipTextStyle colors are BGRA; TitleStyle uses RGBA.
    style.color = {parsed.color[2], parsed.color[1], parsed.color[0],
                   parsed.color[3]};
    style.outline_color = {parsed.outline_color[2],
                           parsed.outline_color[1],
                           parsed.outline_color[0],
                           parsed.outline_color[3]};
    style.outline_px = static_cast<int>(parsed.outline_px + 0.5f);
    style.align = parsed.align == ClipTextStyle::Align::Left
                      ? 0
                      : parsed.align == ClipTextStyle::Align::Right
                            ? 2
                            : 1;
    title_editor_->SetStyle(style);
    ShowOverlay(title_editor_);
}

void VideoEditorAppWidget::AddAdjustmentClip() {
    int track_index = -1;
    for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
        if (project_.tracks[i].kind == TrackKind::Video) {
            track_index = static_cast<int>(i);
            break;
        }
    }
    if (track_index < 0) {
        track_index = history_.AddTrack(TrackKind::Video, "Video");
        if (track_index < 0) {
            SetStatus("Unable to add video track", true);
            return;
        }
    }
    Clip clip;
    clip.id = NewClipId();
    clip.is_adjustment = true;
    clip.track = track_index;
    clip.timeline_start = snap_enabled_
        ? SnapTime(project_, track_index, playhead_, 0.1, playhead_)
        : playhead_;
    clip.duration = 5.0;
    MediaBounds bounds;   // unbounded, no decode path
    bounds.has_video = true;
    std::string error;
    std::string clip_id;
    if (!history_.AddClip(std::move(clip), bounds, &clip_id, &error)) {
        SetStatus("Add adjustment layer failed: " + error, true);
        return;
    }
    SelectClip(clip_id);
    NotifyProjectChanged();
    SetStatus("Added adjustment layer — attach effects in the FX tab");
}

void VideoEditorAppWidget::ToggleShortcutsOverlay() {
    if (!shortcuts_overlay_) return;
    if (shortcuts_overlay_->IsOpen()) {
        shortcuts_overlay_->Close();   // on_close -> HideOverlay()
    } else {
        shortcuts_overlay_->Toggle();
        ShowOverlay(shortcuts_overlay_);
    }
}

void VideoEditorAppWidget::ApplyWorkspace(const WorkspaceLayout& ws) {
    left_panel_visible_ = ws.left_visible;
    right_panel_visible_ = ws.right_visible;
    timeline_fraction_ = std::clamp(ws.timeline_fraction, 0.2, 0.5);
    SetRightTab(ws.right_tab);   // restyles tabs + RequestLayout
    SetStatus("Workspace: " + ws.name);
}

void VideoEditorAppWidget::RefreshWaveformPanel() {
    if (!waveform_panel_) return;
    const Clip* clip = FindClip(project_, selected_clip_id_);
    AudioPeaks peaks;
    const bool has =
        clip != nullptr && clip->source.kind == SourceKind::Media &&
        catalog_.IsOpen() &&
        catalog_.GetAudioPeaks(clip->source_path, &peaks) &&
        peaks.buckets > 0;
    if (has) {
        waveform_panel_->SetPeaks(peaks, clip->timeline_start,
                                  clip->duration);
        waveform_panel_->SetFadeIn(clip->fade_in);
        waveform_panel_->SetFadeOut(clip->fade_out);
        waveform_panel_->SetPlayhead(playhead_);
    }
    if (waveform_panel_->IsVisible() != has) {
        waveform_panel_->SetVisible(has);
        RequestLayout();
    }
}

void VideoEditorAppWidget::RefreshExportQueue() {
    if (export_queue_) export_queue_->SetJobs(export_jobs_);
}

void VideoEditorAppWidget::InitAutosave() {
    if (project_path_.empty()) return;
    // Premiere-style "Auto Save" folder next to the project file.
    const std::string dir = Path::NormalizeUtf8Path(
        (std::filesystem::path(Path::FromUtf8(project_path_))
             .parent_path() /
         "autosave")
            .string());
    std::string error;
    if (!autosave_.Init(project_path_, dir, &error)) {
        SetStatus("Autosave disabled: " + error, true);
    }
}

void VideoEditorAppWidget::SetClipKeyframesBy(
    const std::string& clip_id, KeyProp prop,
    std::vector<Keyframe> keys) {
    std::string error;
    if (!history_.SetClipKeyframes(clip_id, prop, std::move(keys),
                                   &error)) {
        SetStatus("Keyframes failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Keyframes updated");
}

void VideoEditorAppWidget::AddClipEffectBy(const std::string& clip_id,
                                           const ClipEffect& effect) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Select a clip for the effect", true);
        return;
    }
    Clip updated = *clip;
    updated.effects.push_back(effect);
    std::string error;
    if (!history_.ReplaceClip(clip_id, updated,
                              BoundsForPath(clip->source_path), &error)) {
        SetStatus("Add effect failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added effect " + effect.type);
}

void VideoEditorAppWidget::AddMarkerAt(double t,
                                       const std::string& name) {
    Marker marker;
    marker.time = t;
    marker.name = name.empty() ? "Marker" : name;
    std::string error;
    if (!history_.AddMarker(marker, &error)) {
        SetStatus("Marker failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added marker");
}

void VideoEditorAppWidget::RemoveMarkerAt(double t) {
    if (!history_.RemoveMarker(t)) {
        SetStatus("No marker at that time", true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Removed marker");
}

void VideoEditorAppWidget::RenameMarkerAt(double t,
                                          const std::string& name) {
    // AddMarker merges on equal times — remove+add under one macro
    // keeps the rename a single undo step and preserves color.
    std::array<std::uint8_t, 4> color{255, 214, 0, 255};
    for (const Marker& m : project_.markers) {
        if (std::abs(m.time - t) < 1e-6) color = m.color;
    }
    history_.BeginMacro("Rename marker");
    history_.RemoveMarker(t);
    Marker marker;
    marker.time = t;
    marker.name = name;
    marker.color = color;
    std::string error;
    history_.AddMarker(marker, &error);
    history_.EndMacro();
    NotifyProjectChanged();
    SetStatus("Renamed marker");
}

void VideoEditorAppWidget::SetWorkAreaRange(double in_s, double out_s) {
    std::string error;
    if (!history_.SetWorkArea(in_s, out_s, &error)) {
        SetStatus("Work area failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus(out_s <= in_s ? "Work area cleared" : "Work area set");
}

void VideoEditorAppWidget::SetTrackFlag(int track, char which, bool on) {
    if (track < 0 || track >= static_cast<int>(project_.tracks.size())) {
        return;
    }
    std::string error;
    if (which == 'l') {
        if (!history_.SetTrackLocked(track, on, &error)) {
            SetStatus("Lock failed: " + error, true);
            return;
        }
    } else if (which == 'm') {
        project_.tracks[track].muted = on;
    } else {
        project_.tracks[track].hidden = on;
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SetRightTab(int tab) {
    right_tab_ = std::clamp(tab, 0, 6);
    const auto style_for = [this](int index) {
        return right_tab_ == index ? Widgets::ButtonStyle::Secondary
                                   : Widgets::ButtonStyle::Ghost;
    };
    if (tab_inspector_btn_) tab_inspector_btn_->SetStyle(style_for(0));
    if (tab_effects_btn_) tab_effects_btn_->SetStyle(style_for(1));
    if (tab_markers_btn_) tab_markers_btn_->SetStyle(style_for(2));
    if (tab_lab_btn_) tab_lab_btn_->SetStyle(style_for(3));
    if (tab_history_btn_) tab_history_btn_->SetStyle(style_for(4));
    if (tab_mixer_btn_) tab_mixer_btn_->SetStyle(style_for(5));
    if (tab_queue_btn_) tab_queue_btn_->SetStyle(style_for(6));
    RequestLayout();
    Invalidate();
}

void VideoEditorAppWidget::ApplyProjectSettings(
    const ProjectSettings& settings) {
    project_.width = std::max(16, settings.w);
    project_.height = std::max(16, settings.h);
    project_.fps_num = std::max(1, settings.fps_num);
    project_.fps_den = std::max(1, settings.fps_den);
    project_.bg_color = settings.bg;
    NotifyProjectChanged();
    SetStatus("Project settings applied");
}

void VideoEditorAppWidget::AddGeneratedClip(SourceKind kind) {
    int track_index = -1;
    for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
        if (project_.tracks[i].kind == TrackKind::Video) {
            track_index = static_cast<int>(i);
            break;
        }
    }
    if (track_index < 0) {
        track_index = history_.AddTrack(TrackKind::Video, "Video");
        if (track_index < 0) {
            SetStatus("Unable to add video track", true);
            return;
        }
    }
    Clip clip;
    clip.id = NewClipId();
    clip.source.kind = kind;
    clip.track = track_index;
    clip.timeline_start = snap_enabled_
        ? SnapTime(project_, track_index, playhead_, 0.1, playhead_)
        : playhead_;
    clip.duration = 5.0;
    if (kind == SourceKind::ColorMatte) {
        clip.source.param = "#3a5fd9";
    } else if (kind == SourceKind::ColorBars) {
        clip.source.param = "smpte75";
    } else {
        clip.source.param = "Title";
    }
    // Mattes/bars fill the canvas natively; text sizes to content.
    MediaBounds bounds;
    bounds.duration_seconds = 0.0;   // unbounded — generated
    bounds.has_video = true;
    std::string error;
    std::string clip_id;
    if (!history_.AddClip(std::move(clip), bounds, &clip_id, &error)) {
        SetStatus("Add generated clip failed: " + error, true);
        return;
    }
    SelectClip(clip_id);
    NotifyProjectChanged();
    SetStatus("Added generated clip");
}

void VideoEditorAppWidget::AddVst3FxToSelectedClip() {
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr) {
        SetStatus("Select a clip first", true);
        return;
    }
#ifdef _WIN32
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"VST3 plugin", L"*.vst3"), L"Add VST3 effect");
    if (picked.empty()) return;
    Clip updated = *clip;
    ClipAudioFx fx;
    fx.plugin_path = Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
    fx.name = Basename(fx.plugin_path);
    updated.audio_fx.push_back(std::move(fx));
    std::string error;
    if (!history_.ReplaceClip(clip->id, updated,
                              BoundsForPath(clip->source_path), &error)) {
        SetStatus("Add VST3 failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added VST3 effect");
#endif
}

void VideoEditorAppWidget::OpenVst3FxEditor() {
#ifdef _WIN32
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr) {
        SetStatus("Select a clip first", true);
        return;
    }
    const ClipAudioFx* fx = nullptr;
    for (const ClipAudioFx& candidate : clip->audio_fx) {
        if (candidate.enabled) {
            fx = &candidate;
            break;
        }
    }
    if (fx == nullptr) {
        SetStatus("Selected clip has no enabled VST3 effect", true);
        return;
    }
    const auto bounds = GetBounds();
    Vst3EditorRect rect;
    rect.left = static_cast<int>(bounds.x + bounds.width * 0.2f);
    rect.top = static_cast<int>(bounds.y + 60.0f);
    rect.right = static_cast<int>(bounds.x + bounds.width * 0.8f);
    rect.bottom = static_cast<int>(bounds.y + bounds.height * 0.7f);
    std::string error;
    if (!vst3_editor_.Show(*fx, parent_hwnd_, rect, &error)) {
        SetStatus("Plugin editor failed: " + error, true);
        return;
    }
    SetStatus("Opened " + fx->name);
#endif
}

bool VideoEditorAppWidget::RunLabOperation(
    const LabPanel::RunParams& params, const Lab::JobControl& job) {
    // Worker-thread entry (LabPanel AsyncOp): no UI access here — the
    // result PNG path is stashed and imported from Tick on the UI side.
    Compositor compositor;
    std::string error;
    if (!compositor.Init(project_, CompositorOptions{}, &error)) {
        job.Fail("Compositor init failed: " + error);
        return false;
    }
    std::vector<std::uint8_t> bgra;
    if (!compositor.RenderFrameAt(playhead_, &bgra, &error)) {
        job.Fail("Frame render failed: " + error);
        return false;
    }
    video::Frame frame;
    if (!frame.Allocate(compositor.CanvasWidth(),
                        compositor.CanvasHeight(),
                        video::PixelFormat::BGRA8)) {
        job.Fail("Frame allocation failed");
        return false;
    }
    const std::size_t bytes =
        std::min<std::size_t>(bgra.size(), frame.DataSize());
    std::memcpy(frame.Data(), bgra.data(), bytes);

    using LabPanelMode = LabPanel::Mode;
    const Lab::IntRect rect{params.region.x, params.region.y,
                            params.region.w, params.region.h};
    video::Frame result;
    bool ok = true;
    if (params.mode == LabPanelMode::Remove) {
        Lab::InpaintParams ip;
        ip.search_radius = params.search_radius;
        ip.should_cancel = [&job] { return job.ShouldCancel(); };
        ip.report_progress = [&job](float f) { job.ReportProgress(f); };
        if (params.UseLasso()) {
            const auto mask = Lab::PolygonMask(
                frame.Width(), frame.Height(), params.polygon,
                params.feather_px);
            ok = Lab::InpaintMask(frame, mask, ip);
        } else {
            ok = Lab::InpaintRect(frame, rect, ip);
        }
        result = frame;   // shallow? Frame is a class — check copy
    } else if (params.mode == LabPanelMode::Cutout) {
        result = params.UseLasso()
            ? Lab::CutOutLasso(frame, params.polygon, params.feather_px)
            : Lab::CutOutRect(frame, rect);
        ok = result.IsValid();
    } else {
        const auto mask = params.UseLasso()
            ? Lab::PolygonMask(frame.Width(), frame.Height(),
                               params.polygon, params.feather_px)
            : Lab::RectMask(frame.Width(), frame.Height(), rect);
        result = Lab::CopyRegionToLayer(frame, mask);
        ok = result.IsValid();
    }
    if (!ok) {
        job.Fail("Lab operation failed or was cancelled");
        return false;
    }
    // Write the result next to the project (or temp when unsaved).
    std::filesystem::path dir =
        project_path_.empty()
            ? std::filesystem::temp_directory_path()
            : std::filesystem::path(
                  Path::FromUtf8(project_path_)).parent_path();
    static int lab_counter = 0;
    const std::filesystem::path png =
        dir / ("xer_lab_" + std::to_string(++lab_counter) + ".png");
    video::FrameExporter exporter;
    if (!exporter.ToFile(png.string(), result)) {
        job.Fail("PNG export failed");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(lab_mutex_);
        pending_lab_png_ = Path::NormalizeUtf8Path(png.string());
        has_pending_lab_ = true;
    }
    job.ReportProgress(1.0f);
    return true;
}

void VideoEditorAppWidget::Undo() {
    if (history_.Undo()) {
        NotifyProjectChanged();
        SetStatus("Undo");
    } else {
        SetStatus("Nothing to undo");
    }
}

void VideoEditorAppWidget::Redo() {
    if (history_.Redo()) {
        NotifyProjectChanged();
        SetStatus("Redo");
    } else {
        SetStatus("Nothing to redo");
    }
}

void VideoEditorAppWidget::RefreshCatalog() {
    std::string error;
    catalog_entries_ = catalog_.IsOpen() ? catalog_.List(&error)
                                         : std::vector<CatalogEntry>{};
    if (!error.empty()) SetStatus("Catalog: " + error, true);
    if (media_bin_) media_bin_->SetEntries(catalog_entries_);
}

void VideoEditorAppWidget::NotifyProjectChanged() {
    playhead_ = std::clamp(playhead_, 0.0, project_.duration());
    preview_dirty_ = true;   // engine snapshot is stale; rebuilt lazily
    autosave_.MarkDirty();
    timeline_->SetProject(&project_);
    timeline_->SetPlayhead(playhead_);
    timeline_->SetSelectedClip(selected_clip_id_);
    inspector_->SetClip(FindClip(project_, selected_clip_id_));
    inspector_->SetPlayhead(playhead_);
    if (effects_panel_) {
        effects_panel_->SetClip(FindClip(project_, selected_clip_id_));
    }
    if (markers_panel_) {
        markers_panel_->SetProject(&project_);
        markers_panel_->SetPlayhead(playhead_);
    }
    if (history_panel_) {
        history_panel_->SetEntries(history_.UndoLabels(),
                                   history_.RedoLabels());
    }
    if (mixer_panel_) {
        mixer_panel_->SetProject(&project_);
        mixer_panel_->SetMasterGainDb(master_gain_db_);
    }
    RefreshWaveformPanel();
    monitor_->SetProject(project_);
    if (title_bar_) {
        title_bar_->SetCenterCommandValue(
            project_path_.empty() ? "Untitled.xerproj"
                                  : Basename(project_path_));
    }
    Invalidate();
}

void VideoEditorAppWidget::SetStatus(const std::string& text,
                                     bool is_error) {
    if (status_label_) status_label_->SetText(text);
    // Errors additionally surface as toasts; routine info stays on the
    // status bar only.
    if (is_error && status_toast_ && !text.empty()) {
        status_toast_->Push(text, true);
    }
}

MediaBounds VideoEditorAppWidget::BoundsForPath(
    const std::string& path) const {
    if (const CatalogEntry* entry = FindCatalogEntry(path)) {
        return {entry->duration_seconds, entry->HasVideo()};
    }
    XER::Content::Video::VideoInfo info;
    std::string error;
    MediaBounds bounds;
    if (XER::Content::Video::ProbeVideo(path, &info, &error)) {
        bounds.duration_seconds = info.duration_seconds;
        bounds.has_video = info.width > 0 && info.height > 0;
    }
    return bounds;
}

const CatalogEntry* VideoEditorAppWidget::FindCatalogEntry(
    const std::string& path) const {
    const std::string normalized = Path::NormalizeUtf8Path(path);
    const auto it = std::find_if(
        catalog_entries_.begin(), catalog_entries_.end(),
        [&](const CatalogEntry& entry) {
            return Path::NormalizeUtf8Path(entry.path) == normalized;
        });
    return it == catalog_entries_.end() ? nullptr : &*it;
}

void VideoEditorAppWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.background, 0.0f);
    renderer.DrawLine({bounds.x, bounds.y + 82.0f},
                      {bounds.x + bounds.width, bounds.y + 82.0f},
                      tokens.border, 1.0f);
}

void VideoEditorAppWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    InstallGlobalShortcuts();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float gap = tokens.spacing_small;

    title_bar_->Layout({bounds.x, bounds.y, bounds.width, 38.0f}, tokens);

    const float toolbar_y = bounds.y + 38.0f;
    const float toolbar_h = 44.0f;
    float x = bounds.x + 10.0f;
    const auto place = [&](const std::shared_ptr<Widgets::Button>& button,
                           float width) {
        button->Layout({x, toolbar_y + 7.0f, width, 30.0f}, tokens);
        x += width + gap;
    };
    place(import_btn_, 70.0f);
    place(add_btn_, 58.0f);
    place(undo_btn_, 58.0f);
    place(redo_btn_, 58.0f);
    place(split_btn_, 58.0f);
    place(delete_btn_, 66.0f);
    place(snap_btn_, 58.0f);
    place(play_btn_, 64.0f);
    zoom_label_->Layout({x + 4.0f, toolbar_y + 10.0f,
                         std::max(60.0f, bounds.x + bounds.width - 112.0f - x),
                         22.0f}, tokens);
    export_btn_->Layout({bounds.x + bounds.width - 96.0f,
                         toolbar_y + 6.0f, 84.0f, 32.0f}, tokens);

    const float status_h = 26.0f;
    const float content_top = toolbar_y + toolbar_h;
    const float content_bottom = bounds.y + bounds.height - status_h;
    const float timeline_h = std::clamp(
        (content_bottom - content_top) *
            static_cast<float>(timeline_fraction_),
        170.0f, 340.0f);
    // Optional selected-clip waveform strip sits between the central
    // row and the timeline.
    const float wave_h =
        (waveform_panel_ && waveform_panel_->IsVisible()) ? 84.0f : 0.0f;
    const float central_h =
        std::max(80.0f, content_bottom - content_top - timeline_h -
                            gap * 2.0f - wave_h);

    const float left_w = left_panel_visible_
        ? std::clamp(bounds.width * 0.20f, 230.0f, 320.0f)
        : 0.0f;
    const float right_w = right_panel_visible_
        ? std::clamp(bounds.width * 0.22f, 250.0f, 340.0f)
        : 0.0f;
    float panel_x = bounds.x + 10.0f;
    const float panel_y = content_top + gap;
    media_bin_->SetVisible(left_panel_visible_);
    media_bin_->SetPreviewActive(left_panel_visible_);
    inspector_->SetVisible(right_panel_visible_);
    if (left_panel_visible_) {
        media_bin_->Layout({panel_x, panel_y, left_w - gap,
                            std::max(80.0f, central_h)}, tokens);
        panel_x += left_w;
    }
    const float inspector_x =
        bounds.x + bounds.width - 10.0f - right_w + gap;
    const float monitor_w = std::max(
        240.0f, bounds.x + bounds.width - 10.0f - panel_x - right_w);
    monitor_->Layout({panel_x, panel_y, monitor_w,
                      std::max(80.0f, central_h)}, tokens);
    // Right column: tab strip over the active panel — Premiere's
    // stacked panel group (Inspector/Effects/Markers/Lab/History/Mix).
    inspector_->SetVisible(right_panel_visible_ && right_tab_ == 0);
    effects_panel_->SetVisible(right_panel_visible_ && right_tab_ == 1);
    markers_panel_->SetVisible(right_panel_visible_ && right_tab_ == 2);
    lab_panel_->SetVisible(right_panel_visible_ && right_tab_ == 3);
    history_panel_->SetVisible(right_panel_visible_ && right_tab_ == 4);
    mixer_panel_->SetVisible(right_panel_visible_ && right_tab_ == 5);
    export_queue_->SetVisible(right_panel_visible_ && right_tab_ == 6);
    tab_inspector_btn_->SetVisible(right_panel_visible_);
    tab_effects_btn_->SetVisible(right_panel_visible_);
    tab_markers_btn_->SetVisible(right_panel_visible_);
    tab_lab_btn_->SetVisible(right_panel_visible_);
    tab_history_btn_->SetVisible(right_panel_visible_);
    tab_mixer_btn_->SetVisible(right_panel_visible_);
    tab_queue_btn_->SetVisible(right_panel_visible_);
    if (right_panel_visible_) {
        const float tab_h = 30.0f;
        const float tab_w =
            (right_w - gap - gap * 6.0f) / 7.0f;
        std::shared_ptr<Widgets::Button> tabs[] = {
            tab_inspector_btn_, tab_effects_btn_, tab_markers_btn_,
            tab_lab_btn_, tab_history_btn_, tab_mixer_btn_,
            tab_queue_btn_};
        float tx = inspector_x;
        for (const auto& btn : tabs) {
            btn->Layout({tx, panel_y, tab_w, tab_h - 6.0f}, tokens);
            tx += tab_w + gap;
        }
        std::shared_ptr<Widgets::Widget> panels[] = {
            inspector_,  effects_panel_, markers_panel_, lab_panel_,
            history_panel_, mixer_panel_, export_queue_};
        panels[right_tab_]->Layout(
            {inspector_x, panel_y + tab_h, right_w - gap,
             std::max(60.0f, central_h - tab_h)}, tokens);
    }
    if (wave_h > 0.0f) {
        waveform_panel_->Layout(
            {bounds.x + 10.0f, panel_y + central_h + gap,
             bounds.width - 20.0f, wave_h - gap}, tokens);
    }
    timeline_->Layout(
        {bounds.x + 10.0f, panel_y + central_h + gap + wave_h,
         bounds.width - 20.0f, timeline_h}, tokens);
    if (status_toast_) {
        // Click-through overlay spanning the whole window.
        status_toast_->Layout(bounds, tokens);
    }
    status_label_->Layout({bounds.x + 12.0f, content_bottom + 4.0f,
                           bounds.width - 24.0f, 20.0f}, tokens);
}

bool VideoEditorAppWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using Widgets::Event;
    using Widgets::KeyCode;
    if (event.type == Event::Type::TextInput && event.text.size() == 1 &&
        !event.is_ctrl && !event.is_alt) {
        // Premiere tool letters -> timeline tool selection.
        static const struct { char key; ToolId tool; } kToolKeys[] = {
            {'v', ToolId::Selection},  {'c', ToolId::Razor},
            {'b', ToolId::RippleEdit}, {'n', ToolId::RollingEdit},
            {'y', ToolId::Slip},       {'u', ToolId::Slide},
            {'r', ToolId::RateStretch},{'h', ToolId::Hand},
            {'z', ToolId::Zoom},
        };
        const char lower = static_cast<char>(
            std::tolower(static_cast<unsigned char>(event.text.front())));
        for (const auto& binding : kToolKeys) {
            if (lower == binding.key) {
                timeline_->SetTool(binding.tool);
                SetStatus(std::string("Tool: ") +
                          timeline_->ActiveToolName());
                return true;
            }
        }
        switch (event.text.front()) {
            case 'm': case 'M':
                AddMarkerAt(playhead_, "");
                return true;
            case 'i': case 'I':
                SetWorkAreaRange(playhead_, project_.work_out);
                return true;
            case 'o': case 'O':
                SetWorkAreaRange(project_.work_in, playhead_);
                return true;
            case 's': case 'S':
                snap_enabled_ = !snap_enabled_;
                timeline_->SetSnapEnabled(snap_enabled_);
                snap_btn_->SetStyle(snap_enabled_
                                        ? Widgets::ButtonStyle::Secondary
                                        : Widgets::ButtonStyle::Ghost);
                SetStatus(snap_enabled_ ? "Snapping on" : "Snapping off");
                return true;
            case 'j': case 'J':
                Seek(playhead_ - 5.0);
                return true;
            case 'k': case 'K':
                if (monitor_->IsPlaying()) TogglePlay();
                return true;
            case 'l': case 'L':
                Seek(playhead_ + 5.0);
                return true;
            case '?':
                ToggleShortcutsOverlay();
                return true;
            default:
                return false;
        }
    }
    if (event.type != Event::Type::KeyDown) return false;
    if (event.is_ctrl && event.key == KeyCode::Z) {
        if (event.is_shift) Redo(); else Undo();
        return true;
    }
    if (event.is_ctrl && event.key == KeyCode::Y) {
        Redo();
        return true;
    }
    if (event.is_ctrl && event.key == KeyCode::S) {
        SaveProject(event.is_shift);
        return true;
    }
    if (event.is_ctrl && event.key == KeyCode::O) {
        OpenProject();
        return true;
    }
    if (event.is_ctrl && event.key == KeyCode::N) {
        NewProject();
        return true;
    }
    if (event.is_ctrl && event.key == KeyCode::P) {
        StartExport();
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Space) {
        TogglePlay();
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::ArrowLeft) {
        Seek(playhead_ - (event.is_shift ? 5.0 : 1.0 / 30.0));
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::ArrowRight) {
        Seek(playhead_ + (event.is_shift ? 5.0 : 1.0 / 30.0));
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Home) {
        Seek(0.0);
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::End) {
        Seek(project_.duration());
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Delete) {
        DeleteClip(selected_clip_id_);
        return true;
    }
    return false;
}

void VideoEditorAppWidget::Tick(double dt_seconds) {
    if (status_toast_) status_toast_->Tick(dt_seconds);
    // Autosave poll (every ~2 s of wall time; the manager itself
    // enforces its own interval + dirty gating).
    autosave_poll_ += dt_seconds;
    if (autosave_poll_ >= 2.0 && autosave_.IsInitialized()) {
        autosave_poll_ = 0.0;
        const auto now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        std::string error;
        if (!autosave_.MaybeAutosave(project_, now_ms, &error) &&
            !error.empty()) {
            SetStatus("Autosave failed: " + error, true);
        }
    }
    // Marshal PreviewEngine worker callbacks onto widgets.
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        if (has_pending_frame_) {
            monitor_->SetExternalFrame(pending_frame_, pending_frame_w_,
                                       pending_frame_h_);
            has_pending_frame_ = false;
        }
        if (has_pending_peaks_) {
            monitor_->SetPeakLevels(pending_peak_l_, pending_peak_r_);
            if (mixer_panel_) {
                mixer_panel_->SetPeak(-1, pending_peak_l_,
                                      pending_peak_r_);
            }
            has_pending_peaks_ = false;
        }
        if (pending_playhead_ >= 0.0) {
            playhead_ =
                std::clamp(pending_playhead_, 0.0, project_.duration());
            timeline_->SetPlayhead(playhead_);
            inspector_->SetPlayhead(playhead_);
            if (markers_panel_) markers_panel_->SetPlayhead(playhead_);
            if (waveform_panel_ && waveform_panel_->IsVisible()) {
                waveform_panel_->SetPlayhead(playhead_);
            }
            monitor_->SetTime(playhead_, false);
            pending_playhead_ = -1.0;
            if (preview_ready_ && !preview_engine_.Playing() &&
                monitor_->IsPlaying()) {
                // Engine auto-stopped at the end of the timeline.
                monitor_->SetPlaying(false);
                monitor_->SetExternalPlayback(false);
                monitor_->SetTime(playhead_, true);
                play_btn_->SetText("Play");
            }
        }
    }
    // Import a finished Lab result onto the timeline (worker wrote a
    // PNG; catalog + history edits stay on the UI thread).
    {
        std::lock_guard<std::mutex> lock(lab_mutex_);
        if (has_pending_lab_) {
            const std::string png = std::move(pending_lab_png_);
            pending_lab_png_.clear();
            has_pending_lab_ = false;
            std::int64_t id = 0;
            std::string error;
            if (catalog_.Open(&error) &&
                catalog_.Import(png, &id, true, &error)) {
                RefreshCatalog();
                selected_media_path_ = png;
                AddSelectedToTimeline();
                SetStatus("Lab result added to timeline");
            } else {
                SetStatus("Lab import failed: " + error, true);
            }
        }
    }
    if (undo_btn_) {
        undo_btn_->SetEnabled(history_.CanUndo());
        redo_btn_->SetEnabled(history_.CanRedo());
        undo_btn_->SetText(history_.CanUndo()
                               ? "Undo " + history_.UndoLabel()
                               : "Undo");
        redo_btn_->SetText(history_.CanRedo()
                               ? "Redo " + history_.RedoLabel()
                               : "Redo");
    }
    if (export_engine_.IsRunning() && export_dialog_) {
        const auto progress = export_engine_.Progress();
        export_dialog_->SetProgress(progress);
        if (!export_jobs_.empty() &&
            export_jobs_.back().state ==
                static_cast<int>(ExportJobState::Running)) {
            export_jobs_.back().progress =
                static_cast<float>(progress.fraction);
            RefreshExportQueue();
        }
    } else if (export_dialog_ && !export_dialog_->Finished() &&
               export_engine_.Status() != ExportStatus::kIdle) {
        const auto status = export_engine_.Status();
        std::string message;
        if (status == ExportStatus::kDone) {
            message = "Export completed";
        } else if (status == ExportStatus::kCancelled) {
            message = "Export cancelled";
        } else {
            message = export_engine_.Error();
        }
        export_dialog_->SetFinished(status, message);
        if (!export_jobs_.empty()) {
            auto& job = export_jobs_.back();
            job.progress = 1.0f;
            job.state = static_cast<int>(
                status == ExportStatus::kDone ? ExportJobState::Done
                                              : ExportJobState::Failed);
            RefreshExportQueue();
        }
    }
}

}  // namespace XER::VideoEditor::Ui
