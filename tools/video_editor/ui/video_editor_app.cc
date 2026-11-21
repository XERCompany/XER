#include "video_editor_app.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")   // ShellExecuteW (media-bin reveal)
#pragma comment(lib, "comctl32.lib")  // SetWindowSubclass (close guard)
#endif
#endif

#include "app_runtime/app_runtime.h"
#include "app_runtime/ui_manifest.h"
#include "audio_sync.h"
#include "worker_pipe.h"
#include "content/video/video_info.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "generated_source.h"
#include "io/oiio_image.h"
#include "lab/async_op.h"
#include "lab/object_ops.h"
#include "lut_manager.h"
#include "lut_manager.h"
#include "ai/audio_pcm.h"
#include "ai/beat_markers.h"
#include "ai/whisper_transcribe.h"
#include "playback/audio_sink_factory.h"
#include "smoke_checks.h"   // MediaImportExtensions/IsMediaImportPath
#include "timecode.h"       // FormatTimecode (drop-hover status)
#include "js_fx.h"
#include "plugin_host.h"
#include "project_store.h"
#include "extensions/ext_host.h"
#include "rendering/renderer.h"
#include "video/frame_exporter.h"
#include "theme/tokens.h"
#include "widgets/app_title_bar.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/tabs.h"
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

// MediaPrep::Lookup -> the widget's MediaLookup tri-state (the widget
// stays decoupled from the worker header).
MediaLookup ToUiLookup(MediaPrep::Lookup lookup) {
    switch (lookup) {
        case MediaPrep::Lookup::kReady:
            return MediaLookup::kReady;
        case MediaPrep::Lookup::kPending:
            return MediaLookup::kPending;
        case MediaPrep::Lookup::kFailed:
            break;
    }
    return MediaLookup::kFailed;
}

// UI state the lab worker needs, snapshotted on the UI thread by the
// lab panel's on_run_prepare hook and carried through
// RunParams::context — RunLabOperation reads ONLY this, never the
// live project_/playhead_/project_path_ (the UI thread mutates those
// through history_ while the job runs).
struct LabSnapshot {
    Project project;
    double playhead = 0.0;
    std::string project_path;
};

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

// "Media" picker pattern built from the shared extension list
// (smoke_checks.h) so the file-dialog filter and the WM_DROPFILES
// filter can never drift apart. Yields "*.mp4;*.m4v;…;*.ora".
const std::wstring& MediaImportPattern() {
    static const std::wstring pattern = [] {
        std::wstring out;
        bool need_glob = true;
        for (char c : MediaImportExtensions()) {
            if (need_glob) {
                out += L"*.";   // first char of each ;-token
                need_glob = false;
            }
            if (c == ';') {
                out.push_back(L';');
                need_glob = true;
            } else {
                out.push_back(static_cast<wchar_t>(c));
            }
        }
        return out;
    }();
    return pattern;
}

// Subclass id + proc for the unsaved-changes close guard and the OS
// file-drop import. WM_CLOSE arrives at subclass procs before the
// window's own WindowProc — returning 0 vetoes the destroy so the
// save/discard/cancel prompt (ConfirmDiscardChanges, reached through
// the bound thunks ref_data points at) can cancel a close.
// WM_DROPFILES (enabled via DragAcceptFiles in the ctor) is drained
// here — DragQueryFileW paths -> the file_drop thunk — so the drop
// never reaches the window's default handling. No WM_NCDESTROY work
// is needed: comctl32 unwinds the subclass chain itself during
// window teardown, and the widget dtor RemoveWindowSubclass's first
// whenever the HWND outlives it (the thunk payload is a member, so a
// late message can never reach dead state).
constexpr UINT_PTR kCloseGuardSubclassId = 0x584552;   // 'XER'
LRESULT CALLBACK CloseGuardSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*subclass_id*/, DWORD_PTR ref_data) {
    const auto* hooks =
        reinterpret_cast<const WindowSubclassHooks*>(ref_data);
    if (msg == WM_CLOSE && hooks != nullptr && hooks->close_guard &&
        !hooks->close_guard()) {
        return 0;
    }
    if (msg == WM_DROPFILES && hooks != nullptr && hooks->file_drop) {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        std::vector<std::wstring> paths;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            if (len == 0) continue;
            std::wstring buf(len + 1, L'\0');
            DragQueryFileW(drop, i, buf.data(), len + 1);
            buf.resize(len);
            paths.push_back(std::move(buf));
        }
        DragFinish(drop);
        // Runs on the UI thread — the window proc and the widget
        // tree share the app.Run() message pump, so the thunk may
        // touch widget state directly.
        hooks->file_drop(paths);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
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

// Minimal 16-bit PCM WAV writer for audio-plugin bounces — avoids an
// encoder dependency for a bounce that the catalog re-imports anyway.
bool WriteWav16(const std::string& path, const float* samples,
                int frames, int channels, int sample_rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(frames * channels * 2);
    const auto w32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto w16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + data_bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    w32(16);
    w16(1);  // PCM
    w16(static_cast<std::uint16_t>(channels));
    w32(static_cast<std::uint32_t>(sample_rate));
    w32(static_cast<std::uint32_t>(sample_rate * channels * 2));
    w16(static_cast<std::uint16_t>(channels * 2));
    w16(16);
    std::fwrite("data", 1, 4, f);
    w32(data_bytes);
    for (int i = 0; i < frames * channels; ++i) {
        const float s = std::clamp(samples[i], -1.0f, 1.0f);
        const std::int16_t v = static_cast<std::int16_t>(s * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

// Undoable project-settings edit (canvas size, fps, clear color) —
// the five fields the dialog mutates snapshot before Apply and restore
// verbatim on Revert, same contract as SetWorkAreaCommand in
// timeline_history.cc. `next` arrives pre-clamped by
// ApplyProjectSettings; history_.Execute() runs the first Apply.
class SetProjectSettingsCommand : public Command {
public:
    explicit SetProjectSettingsCommand(ProjectSettings next)
        : next_(std::move(next)) {}
    bool Apply(Project& project) override {
        before_.w = project.width;
        before_.h = project.height;
        before_.fps_num = project.fps_num;
        before_.fps_den = project.fps_den;
        before_.bg = project.bg_color;
        ApplyTo(project, next_);
        return true;
    }
    bool Revert(Project& project) override {
        ApplyTo(project, before_);
        return true;
    }
    std::string Label() const override { return "Project settings"; }

private:
    static void ApplyTo(Project& project, const ProjectSettings& s) {
        project.width = s.w;
        project.height = s.h;
        project.fps_num = s.fps_num;
        project.fps_den = s.fps_den;
        project.bg_color = s.bg;
    }
    ProjectSettings before_;
    ProjectSettings next_;
};

}  // namespace

// Clip + horizontal wheel-scroll host for the right-panel tab strip.
// Widgets::Tabs lays its cells out at natural text width, which can
// exceed a narrow docked panel — the host clips the strip to the
// panel's width (SetClipsChildren clips descendants for both
// rendering and hit-testing) and translates it by a scroll offset so
// every tab stays reachable. Wheel events the Tabs child doesn't
// consume bubble up the parent chain to here (widget_tree.cc).
class TabStripScroll final : public XER::UI::Widgets::Widget {
public:
    explicit TabStripScroll(
        std::shared_ptr<XER::UI::Widgets::Widget> content)
        : content_(std::move(content)) {
        SetClipsChildren(true);
        SetFocusable(false);
        if (content_) AddChild(content_);
    }

    // Centers the content fraction f (0..1) in the viewport on the
    // next layout — SetRightTab passes the active index so
    // programmatic switches (View menu, extension UI opens) reveal
    // the tab instead of selecting it off-screen.
    void RevealFraction(float fraction) {
        pending_reveal_ = std::clamp(fraction, 0.0f, 1.0f);
        RequestLayout();
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer&,
                const XER::UI::Theme::Tokens&) override {}

    void Layout(const XER::UI::Layout::Rect& bounds) override {
        Layout(bounds, CachedLayoutTokens());
    }

    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override {
        SetBounds(bounds);
        if (content_ == nullptr) return;
        const float width =
            std::max(bounds.width, content_->GetPreferredSize().width);
        max_scroll_ = std::max(0.0f, width - bounds.width);
        if (pending_reveal_ >= 0.0f) {
            scroll_ = std::clamp(
                pending_reveal_ * width - bounds.width * 0.5f, 0.0f,
                max_scroll_);
            pending_reveal_ = -1.0f;
        }
        scroll_ = std::clamp(scroll_, 0.0f, max_scroll_);
        content_->Layout({bounds.x - scroll_, bounds.y, width,
                          bounds.height},
                         tokens);
    }

    bool HandleEvent(const XER::UI::Widgets::Event& event) override {
        if (event.type == XER::UI::Widgets::Event::Type::MouseWheel &&
            max_scroll_ > 0.0f &&
            GetBounds().Contains(event.position)) {
            scroll_ = std::clamp(
                scroll_ - event.wheel_delta / 120.0f * kScrollStep,
                0.0f, max_scroll_);
            RequestLayout();
            Invalidate();
            return true;
        }
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, 30.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return content_ != nullptr
                   ? content_->GetPreferredSize()
                   : XER::UI::Layout::Size{0.0f, 30.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 30.0f};
    }

private:
    static constexpr float kScrollStep = 48.0f;
    std::shared_ptr<XER::UI::Widgets::Widget> content_;
    float scroll_ = 0.0f;
    float max_scroll_ = 0.0f;
    float pending_reveal_ = -1.0f;
};

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

// Source monitor's PreviewEngine callbacks — same marshal-to-Tick
// discipline as EngineSink, minus the meters (the source monitor has
// no meter chrome in v1).
class VideoEditorAppWidget::SourceEngineSink final
    : public PreviewEngine::Sink {
public:
    explicit SourceEngineSink(VideoEditorAppWidget* app) : app_(app) {}
    void OnVideoFrame(const video::Frame& frame) override {
        app_->OnSourceEngineFrame(frame);
    }
    void OnAudioPeakLevels(float, float) override {}
    void OnPlayhead(double t) override {
        app_->OnSourceEnginePlayhead(t);
    }

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
    // One-shot crash-recovery offer: surfaces autosave slots without
    // auto-loading (status bar + File ▸ Recover Autosave).
    CheckAutosaveRecovery();
#ifdef _WIN32
    // Unsaved-changes close guard: the title-bar close button and
    // Alt+F4 post WM_CLOSE straight at the window (main.cc), whose
    // handler destroys it unconditionally — a comctl32 subclass lets
    // the save/discard/cancel prompt veto that instead. The thunk
    // member must be bound before install: the proc's ref_data points
    // at it, not at `this`. The same proc drains WM_DROPFILES — the
    // file_drop thunk converts+imports on the UI thread.
    window_hooks_.close_guard = [this] { return ConfirmDiscardChanges(); };
    window_hooks_.file_drop =
        [this](const std::vector<std::wstring>& dropped) {
            OnOsFilesDropped(dropped);
        };
    if (parent_hwnd_ != nullptr) {
        SetWindowSubclass(
            static_cast<HWND>(parent_hwnd_), &CloseGuardSubclassProc,
            kCloseGuardSubclassId,
            reinterpret_cast<DWORD_PTR>(&window_hooks_));
        // OS file drops land as WM_DROPFILES on the top-level HWND —
        // enabled once here, revoked in the dtor while the HWND is
        // still valid.
        DragAcceptFiles(static_cast<HWND>(parent_hwnd_), TRUE);
    }
#endif
}

VideoEditorAppWidget::~VideoEditorAppWidget() {
#ifdef _WIN32
    // Normal teardown destroys the HWND first (RemoveWindowSubclass
    // then fails silently — the IsWindow guard just keeps it tidy).
    // When the window outlives the widget instead, the subclass is
    // removed AND drag-accept revoked so the HWND can't advertise a
    // drop target nobody services.
    if (parent_hwnd_ != nullptr &&
        IsWindow(static_cast<HWND>(parent_hwnd_))) {
        DragAcceptFiles(static_cast<HWND>(parent_hwnd_), FALSE);
        RemoveWindowSubclass(static_cast<HWND>(parent_hwnd_),
                             &CloseGuardSubclassProc,
                             kCloseGuardSubclassId);
    }
#endif
    // The lab worker's on_run calls back into this widget — tear its
    // panel down (AsyncOp dtor cancels + joins) while lab_mutex_ and
    // friends are still valid, instead of letting ~Widget destroy it
    // after the members it touches are gone.
    if (lab_panel_) {
        RemoveChild(lab_panel_);
        lab_panel_.reset();
    }
    preview_engine_.Shutdown();
    source_engine_.Shutdown();
    export_engine_.Cancel();
    export_engine_.Wait();
}

void VideoEditorAppWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    // assets/ui/layout.yaml — seed panel defaults once here; later
    // toggles (View menu, workspaces, monitor focus) own the values.
    const auto& layout_cfg = AppRuntime::SharedLayoutConfig();
    left_panel_visible_ = layout_cfg.left_visible;
    right_panel_visible_ = layout_cfg.right_visible;
    if (layout_cfg.right_tab >= 0) {   // 0 = Inspector; -1 = keep
        right_tab_ = layout_cfg.right_tab;
    }
    // Splitter size overrides start at the manifest widths (0 = use the
    // proportional defaults); splitter drags rewrite them afterward.
    left_width_ = layout_cfg.left_width > 0
        ? static_cast<float>(layout_cfg.left_width) : 0.0f;
    right_width_ = layout_cfg.right_width > 0
        ? static_cast<float>(layout_cfg.right_width) : 0.0f;

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

    // assets/ui/toolbar.json — toolbar_ids orders the left button row
    // (export stays pinned right). No runtime action registry exists,
    // so ids map onto the compiled buttons by position; ids that match
    // nothing become disabled placeholders keeping the slot visible,
    // and compiled buttons the manifest didn't list stay appended.
    toolbar_row_ = {
        {import_btn_, 70.0f}, {add_btn_, 58.0f}, {undo_btn_, 58.0f},
        {redo_btn_, 58.0f},   {split_btn_, 58.0f},
        {delete_btn_, 66.0f}, {snap_btn_, 58.0f}, {play_btn_, 64.0f},
    };
    const auto& toolbar_ids = AppRuntime::SharedAssetsConfig().toolbar_ids;
    if (!toolbar_ids.empty()) {
        static const std::array<const char*, 8> kActionIds = {
            "file.import",    "timeline.add",  "edit.undo",
            "edit.redo",      "edit.split",    "edit.delete",
            "timeline.snap",  "playback.toggle"};
        std::vector<std::pair<std::shared_ptr<Widgets::Button>, float>>
            ordered;
        std::array<bool, 8> used{};
        for (const std::string& id : toolbar_ids) {
            bool matched = false;
            for (std::size_t i = 0; i < kActionIds.size(); ++i) {
                if (!used[i] && id == kActionIds[i]) {
                    used[i] = true;
                    matched = true;
                    ordered.push_back(toolbar_row_[i]);
                }
            }
            if (!matched) {
                auto placeholder = MakeButton(
                    id, Widgets::ButtonStyle::Ghost, [] {},
                    "No action registered for \"" + id + "\"");
                placeholder->SetEnabled(false);
                AddChild(placeholder);
                ordered.emplace_back(std::move(placeholder), 84.0f);
            }
        }
        for (std::size_t i = 0; i < toolbar_row_.size(); ++i) {
            if (!used[i]) ordered.push_back(toolbar_row_[i]);
        }
        toolbar_row_ = std::move(ordered);
    }
    status_label_ = std::make_shared<Widgets::Label>("Ready", true);
    // Status-strip cancel affordance — docked at the strip's right
    // edge and visible only while the audio-sync job runs ("Syncing
    // audio… [Cancel]"). Ghost keeps it an action link in the bar,
    // not another toolbar button.
    sync_cancel_btn_ = MakeButton(
        "Cancel", Widgets::ButtonStyle::Ghost,
        [this] { RequestSyncCancel(); },
        "Cancel the running audio sync (Esc)");
    sync_cancel_btn_->SetVisible(false);
    zoom_label_ = std::make_shared<Widgets::Label>(
        "Wheel: zoom  Shift+Wheel: scroll", true);

    MediaBinWidget::Hooks media_hooks;
    media_hooks.on_import = [this] { ImportMedia(); };
    media_hooks.on_import_url = [this] { ImportFromUrl(); };
    media_hooks.on_add_to_timeline = [this](const std::string& path) {
        AddMediaToTimeline(path);
    };
    media_hooks.on_select = [this](const std::string& path) {
        selected_media_path_ = path;
        SetStatus("Selected media: " + Basename(path));
    };
    media_hooks.on_open_source = [this](const std::string& path) {
        OpenInSourceMonitor(path);
    };
    media_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    media_hooks.on_detect_beats = [this](const std::string& path) {
        DetectBeatsForMedia(path);
    };
    media_hooks.on_transcribe = [this](const std::string& path) {
        TranscribeMedia(path);
    };
    media_hooks.transcribe_available = [] {
        return Ai::WhisperAvailable() && !Ai::FindWhisperModel().empty();
    };
    media_hooks.audio_fx_list = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const PluginDesc* desc : SharedPluginHost().OfKind(
                 XER_PLUGIN_AUDIO_FX)) {
            out.emplace_back(desc->id, desc->name);
        }
        return out;
    };
    media_hooks.on_audio_fx = [this](const std::string& path,
                                     const std::string& plugin_id) {
        BounceAudioFx(path, plugin_id);
    };
    // Bin->timeline drag: card drag released over the timeline adds
    // the media at the drop time; hover drives the accent affordance.
    media_hooks.on_drop = [this](const std::string& path,
                                 XER::UI::Layout::Point position) {
        OnBinMediaDrop(path, position);
    };
    media_hooks.on_drag_hover = [this](XER::UI::Layout::Point position,
                                       bool active) {
        OnBinDragHover(position, active);
    };
    // Bin tile waveforms read the same non-blocking media-prep cache
    // as the timeline's (PeaksLookup mirrors Lookup's order).
    media_hooks.get_audio_peaks =
        [this](const std::string& path, AudioPeaks* out) {
            return static_cast<MediaBinWidget::PeaksLookup>(
                media_prep_.LookupPeaks(path, out));
        };
    // CapCut-style icon rail on the far left — switches the left panel
    // content and jumps to the matching right-side tab.
    nav_rail_ = std::make_shared<NavRailWidget>();
    std::vector<NavRailWidget::Item> rail_top = {
            {NavRailWidget::Glyph::Media, "Media library",
             [this] {
                 left_panel_visible_ = !left_panel_visible_;
                 Invalidate();
             },
             [this] { return left_panel_visible_; }},
            {NavRailWidget::Glyph::Audio, "Audio mixer",
             [this] {
                 right_panel_visible_ = true;
                 SetRightTab(5);
                 Invalidate();
             },
             [this] {
                 return right_panel_visible_ && right_tab_ == 5;
             }},
            {NavRailWidget::Glyph::Text, "Add title",
             [this] {
                 AddGeneratedClip(SourceKind::Text);
                 OpenTitleEditor();
             },
             nullptr},
            {NavRailWidget::Glyph::Fx, "Effects",
             [this] {
                 right_panel_visible_ = true;
                 SetRightTab(1);
                 Invalidate();
             },
             [this] {
                 return right_panel_visible_ && right_tab_ == 1;
             }},
            {NavRailWidget::Glyph::Trans, "Markers",
             [this] {
                 right_panel_visible_ = true;
                 SetRightTab(2);
                 Invalidate();
             },
             [this] {
                 return right_panel_visible_ && right_tab_ == 2;
             }},
    };
    std::vector<NavRailWidget::Item> rail_bottom = {
            {NavRailWidget::Glyph::Settings, "Project settings",
             [this] { ShowProjectSettingsDialog(); },
             nullptr},
    };
    // assets/ui/layout.yaml — NavRail.items reorders/filters the rail.
    // Item carries no id, so yaml names map onto the compiled slots by
    // position (top: media/audio/text/fx/markers, bottom: settings).
    if (!layout_cfg.nav_items.empty()) {
        static const std::array<const char*, 5> kTopIds = {
            "media", "audio", "text", "fx", "markers"};
        static const std::array<const char*, 1> kBottomIds = {
            "settings"};
        std::array<bool, 5> top_used{};
        std::array<bool, 1> bottom_used{};
        std::vector<NavRailWidget::Item> top_out;
        std::vector<NavRailWidget::Item> bottom_out;
        for (const std::string& raw : layout_cfg.nav_items) {
            std::string id = raw;
            std::transform(id.begin(), id.end(), id.begin(),
                           [](unsigned char c) {
                               return std::tolower(c);
                           });
            for (std::size_t i = 0; i < rail_top.size(); ++i) {
                if (!top_used[i] && id == kTopIds[i]) {
                    top_used[i] = true;
                    top_out.push_back(std::move(rail_top[i]));
                }
            }
            for (std::size_t i = 0; i < rail_bottom.size(); ++i) {
                if (!bottom_used[i] && id == kBottomIds[i]) {
                    bottom_used[i] = true;
                    bottom_out.push_back(std::move(rail_bottom[i]));
                }
            }
        }
        rail_top = std::move(top_out);
        rail_bottom = std::move(bottom_out);
    }
    nav_rail_->SetItems(std::move(rail_top), std::move(rail_bottom));
    media_bin_ =
        std::make_shared<MediaBinWidget>(parent_hwnd_, media_hooks);
    // Entry-action setters added by the media-bin pass (parallel fix
    // round): catalog removal + Explorer reveal from row entries.
    media_bin_->SetOnRemoveEntry([this](const std::string& path) {
        const CatalogEntry* entry = FindCatalogEntry(path);
        if (entry == nullptr) {
            SetStatus("Not in catalog: " + Basename(path), true);
            return;
        }
        std::string error;
        // MediaCatalog::Remove is keyed by row id (media_catalog.cc).
        if (!catalog_.Remove(entry->id, true, &error)) {
            SetStatus("Remove failed: " + error, true);
            return;
        }
        // Media left the catalog entirely — drop queued decodes AND
        // every cached poster/peaks entry for it.
        media_prep_.PurgePath(entry->path);
        if (selected_media_path_ == path) selected_media_path_.clear();
        // Same for a clip loaded in the source monitor: its engine
        // holds an open decoder on the file.
        if (!source_path_.empty() &&
            source_path_ == Path::NormalizeUtf8Path(path)) {
            StopSourceTransport();
            source_engine_.Shutdown();
            source_ready_ = false;
            source_path_.clear();
            if (source_monitor_) source_monitor_->ClearSource();
        }
        RefreshCatalog();
        SetStatus("Removed " + Basename(path) + " from catalog");
    });
    media_bin_->SetOnRevealEntry([this](const std::string& path) {
#ifdef _WIN32
        // explorer /select,<path> — one argument, native separators.
        const std::wstring wpath =
            Path::FromUtf8(Path::NormalizeUtf8Path(path)).wstring();
        const std::wstring args = L"/select,\"" + wpath + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(),
                      nullptr, SW_SHOWNORMAL);
        SetStatus("Revealed " + Basename(path));
#else
        (void)path;
        SetStatus("Reveal in Explorer is Windows-only", true);
#endif
    });

    MonitorWidget::Hooks monitor_hooks;
    monitor_hooks.on_time_changed = [this](double t) {
        monitor_target_ = MonitorTarget::Program;
        playhead_ = std::clamp(t, 0.0, project_.duration());
        timeline_->SetPlayhead(playhead_);
        // Same sink set as Seek(), minus the engine rebase — this
        // callback originates from the monitor's own transport, so
        // routing through Seek() would fight the engine.
        inspector_->SetPlayhead(playhead_);
        if (markers_panel_) markers_panel_->SetPlayhead(playhead_);
        if (waveform_panel_ && waveform_panel_->IsVisible()) {
            waveform_panel_->SetPlayhead(playhead_);
        }
    };
    monitor_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    monitor_hooks.on_toggle_focus = [this] {
        if (monitor_focus_) {
            left_panel_visible_ = saved_left_panel_;
            right_panel_visible_ = saved_right_panel_;
            monitor_focus_ = false;
            SetStatus("Panels restored");
        } else {
            saved_left_panel_ = left_panel_visible_;
            saved_right_panel_ = right_panel_visible_;
            left_panel_visible_ = false;
            right_panel_visible_ = false;
            monitor_focus_ = true;
            SetStatus("Monitor focus — press again to restore panels");
        }
        Invalidate();
    };
    monitor_ = std::make_shared<MonitorWidget>(monitor_hooks);
    // Transport setters added by the monitor pass (parallel fix
    // round): on-monitor play toggle, ruler seeks and the |◀ ▶|
    // frame-step buttons all route through the app's engine paths.
    monitor_->SetOnPlayToggle([this] {
        monitor_target_ = MonitorTarget::Program;
        TogglePlay();
    });
    monitor_->SetOnSeekRequest([this](double t) {
        monitor_target_ = MonitorTarget::Program;
        Seek(t);
    });
    monitor_->SetOnFrameStep([this](int dir) {
        monitor_target_ = MonitorTarget::Program;
        // ±1 frame via the project's fps (fps_den/fps_num seconds).
        const double frame_dt =
            project_.fps_num > 0
                ? static_cast<double>(project_.fps_den) /
                      static_cast<double>(project_.fps_num)
                : 1.0 / 30.0;
        Seek(playhead_ + dir * frame_dt);
    });
    // Monitor context-menu quality pick (badge + checkmarks live in
    // the widget): 1.0 / 0.5 / 0.25 render scale. EnsurePreviewEngine
    // re-applies preview_quality_ after every Init — the engine resets
    // its quality command there.
    monitor_->SetOnQualityChange([this](float scale) {
        preview_quality_ = scale;
        preview_engine_.SetQuality(scale);
    });

    // Source monitor (Premiere-style): a second PreviewEngine decodes
    // the loaded media file through a synthetic one-clip project and
    // pumps frames into this widget — every transport action routes
    // back into the app's Source* methods, which also flip the
    // keyboard-transport target to Source.
    SourceMonitorWidget::Hooks source_hooks;
    source_hooks.on_activate = [this] {
        monitor_target_ = MonitorTarget::Source;
    };
    source_hooks.on_seek_request = [this](double t) {
        monitor_target_ = MonitorTarget::Source;
        SourceSeek(t);
    };
    source_hooks.on_play_toggle = [this] {
        monitor_target_ = MonitorTarget::Source;
        SourceTogglePlay();
    };
    source_hooks.on_frame_step = [this](int dir) {
        monitor_target_ = MonitorTarget::Source;
        const double frame_dt =
            source_fps_num_ > 0
                ? static_cast<double>(source_fps_den_) /
                      static_cast<double>(source_fps_num_)
                : 1.0 / 30.0;
        SourceSeek(source_playhead_ + dir * frame_dt);
    };
    source_hooks.on_mark_in = [this] {
        monitor_target_ = MonitorTarget::Source;
        SourceMark(true);
    };
    source_hooks.on_mark_out = [this] {
        monitor_target_ = MonitorTarget::Source;
        SourceMark(false);
    };
    source_hooks.on_insert = [this] {
        monitor_target_ = MonitorTarget::Source;
        EditSourceToTimeline(true);
    };
    source_hooks.on_overwrite = [this] {
        monitor_target_ = MonitorTarget::Source;
        EditSourceToTimeline(false);
    };
    source_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    source_monitor_ =
        std::make_shared<SourceMonitorWidget>(source_hooks);

    InspectorWidget::Hooks inspector_hooks;
    inspector_hooks.on_apply_clip = [this](const Clip& clip) {
        ApplyClip(clip);
    };
    inspector_hooks.on_delete_clip = [this](const std::string& id) {
        DeleteClip(id);
    };
    inspector_hooks.on_add_transition =
        [this](const std::string& id, Transition::Type type,
               double duration, const std::string& script) {
            AddTransitionAfter(id, type, duration, script);
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
    inspector_hooks.on_open_audio_fx_editor =
        [this](const std::string& id, std::size_t index) {
            OpenVst3FxEditor(id, static_cast<int>(index));
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
    // Runs on the UI thread inside LabPanel::StartRun, before the
    // worker is dispatched — copy everything the worker will touch.
    lab_hooks.on_run_prepare = [this](LabPanel::RunParams* params) {
        auto snapshot = std::make_shared<LabSnapshot>();
        snapshot->project = project_;
        snapshot->playhead = playhead_;
        snapshot->project_path = project_path_;
        params->context = std::move(snapshot);
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
        std::string error;
        if (!history_.SetTrackGainDb(track, db, &error)) {
            if (!error.empty()) SetStatus("Gain failed: " + error, true);
            return;
        }
        // Continuous fader writes — invalidate the engine snapshot but
        // skip the full Notify rebuild while the drag is live.
        preview_dirty_ = true;
        Invalidate();
    };
    mixer_hooks.on_track_pan = [this](int track, float pan) {
        std::string error;
        if (!history_.SetTrackPan(track, pan, &error)) {
            if (!error.empty()) SetStatus("Pan failed: " + error, true);
            return;
        }
        preview_dirty_ = true;
        Invalidate();
    };
    mixer_hooks.on_track_mute = [this](int track, bool on) {
        SetTrackFlag(track, 'm', on);
    };
    mixer_hooks.on_master_gain = [this](float db) {
        std::string error;
        if (!history_.SetMasterGainDb(db, &error)) {
            if (!error.empty()) {
                SetStatus("Master gain failed: " + error, true);
            }
            return;
        }
        preview_dirty_ = true;
        // Live-forward to the running engine; Init resets the command,
        // so EnsurePreviewEngine re-applies project_.master_gain_db.
        preview_engine_.SetMasterGainDb(project_.master_gain_db);
        Invalidate();
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

    UrlImportDialog::Hooks url_hooks;
    url_hooks.on_apply = [this](const std::string& url) {
        HideOverlay();
        StartUrlImport(url);
    };
    url_hooks.on_cancel = [this] { HideOverlay(); };
    url_dialog_ = std::make_shared<UrlImportDialog>(url_hooks);

    TitleEditor::Hooks title_hooks;
    title_hooks.on_apply = [this](const TitleStyle& style) {
        HideOverlay();
        Clip* clip = FindClip(project_, selected_clip_id_);
        if (clip == nullptr || clip->source.kind != SourceKind::Text) {
            return;
        }
        Clip updated = *clip;
        updated.source.param =
            SerializeClipTextStyle(TitleStyleToClipStyle(style));
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

    FontManager::Hooks font_hooks;
    font_hooks.on_apply =
        [this](const std::string& family, int weight, bool italic) {
            HideOverlay();
            Clip* clip = FindClip(project_, selected_clip_id_);
            if (clip == nullptr ||
                clip->source.kind != SourceKind::Text) {
                SetStatus("Select a title clip first", true);
                return;
            }
            Clip updated = *clip;
            ClipTextStyle style =
                ParseClipTextStyle(updated.source.param);
            style.font_family = family;
            style.font_weight = weight;
            style.italic = italic;
            updated.source.param = SerializeClipTextStyle(style);
            std::string error;
            if (!history_.ReplaceClip(
                    updated.id, updated,
                    BoundsForPath(updated.source_path), &error)) {
                SetStatus("Font update failed: " + error, true);
                return;
            }
            NotifyProjectChanged();
            SetStatus("Font set to " + family);
        };
    font_hooks.on_cancel = [this] { HideOverlay(); };
    font_manager_ = std::make_shared<FontManager>(font_hooks);

    NewLayerDialog::Hooks layer_hooks;
    layer_hooks.on_apply = [this](const NewLayerSpec& spec) {
        HideOverlay();
        AddGeneratedClipAt(SourceKind::ColorMatte,
                           pending_layer_track_, pending_layer_time_,
                           spec.name, spec.color_hex,
                           spec.duration_seconds);
        pending_layer_track_ = -1;
        pending_layer_time_ = -1.0;
    };
    layer_hooks.on_cancel = [this] {
        pending_layer_track_ = -1;
        pending_layer_time_ = -1.0;
        HideOverlay();
    };
    new_layer_dialog_ = std::make_shared<NewLayerDialog>(layer_hooks);

    LayersPanelWidget::Hooks layers_hooks;
    layers_hooks.on_select_clip = [this](const std::string& id) {
        SelectClip(id);
    };
    layers_hooks.on_seek = [this](double t) { Seek(t); };
    layers_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    layers_panel_ = std::make_shared<LayersPanelWidget>(layers_hooks);

    ExportQueuePanelWidget::Hooks queue_hooks;
    queue_hooks.on_cancel = [this](int) { export_engine_.Cancel(); };
    queue_hooks.on_reveal = [this](int job) {
        if (job < 0 || job >= static_cast<int>(export_jobs_.size())) {
            return;
        }
        const std::string& output = export_jobs_[job].output_path;
        if (output.empty()) {
            SetStatus(export_jobs_[job].label);
            return;
        }
#ifdef _WIN32
        // explorer /select,<path> — same reveal the media bin uses.
        const std::wstring wpath =
            Path::FromUtf8(Path::NormalizeUtf8Path(output)).wstring();
        const std::wstring args = L"/select,\"" + wpath + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(),
                      nullptr, SW_SHOWNORMAL);
        SetStatus("Revealed " + Basename(output));
#else
        SetStatus("Reveal in Explorer is Windows-only", true);
#endif
    };
    queue_hooks.on_retry = [this](int job) { RetryExport(job); };
    queue_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    export_queue_ =
        std::make_shared<ExportQueuePanelWidget>(queue_hooks);
    waveform_panel_ = std::make_shared<WaveformPanelWidget>();
    status_toast_ = std::make_shared<StatusToastWidget>();
    // Full-window click-through overlay: disabled widgets are skipped by
    // FindHitTarget but still rendered, so toasts draw above panels while
    // clicks fall through to the widgets beneath.
    status_toast_->SetEnabled(false);

    // Right-panel tab strip: shadcn-style Widgets::Tabs in Underline
    // mode — the sliding 2px indicator replaces the hand-rolled
    // button row + drawn accent underline this strip used before.
    // Natural-width cells can exceed a narrow docked panel, so the
    // strip sits inside a clip/wheel-scroll host that keeps every
    // tab reachable. SetOnChanged -> SetRightTab preserves the
    // panel-visibility switch; programmatic SetRightTab drives
    // SetActiveIndex (which never fires the callback — no recursion).
    right_tabs_ = std::make_shared<Widgets::Tabs>();
    right_tabs_->SetStyle(Widgets::Tabs::Style::Underline);
    // 0 Insp, 1 FX, 2 Mkr, 3 Lab, 4 Hist, 5 Mix, 6 Queue, 7 Plugins,
    // 8 Color, 9 Web, 10 Lyr — the right_tab_ indexing documented in
    // the header.
    for (const char* label :
         {"Insp", "FX", "Mkr", "Lab", "Hist", "Mix", "Q", "Plg",
          "Color", "Web", "Lyr"}) {
        right_tabs_->AddTab(label);
    }
    right_tabs_->SetActiveIndex(std::clamp(right_tab_, 0, 10));
    right_tabs_->SetOnChanged(
        [this](int index) { SetRightTab(index); });
    tab_strip_ = std::make_shared<TabStripScroll>(right_tabs_);

    PluginsPanelWidget::Hooks plugin_hooks;
    plugin_hooks.on_add_effect = [this](const std::string& type) {
        ClipEffect fx;
        fx.type = type;
        AddClipEffectBy(selected_clip_id_, fx);
    };
    plugins_panel_ = std::make_shared<PluginsPanelWidget>(plugin_hooks);
    plugins_panel_->Reload();

    // LUT manager: scan the search dirs (project/assets/exe/user
    // luts) and register each file as a LUT-backed OcioProfile — the
    // color panel's LOOKS section and ocio:<name> clip effects share
    // that one table. Runs before the panel's Create() so its first
    // Refresh() already sees the scanned looks.
    {
        if (!project_path_.empty()) {
            Lut::Manager::Shared().SetProjectDir(
                Path::FromUtf8(project_path_).parent_path());
        }
        const std::size_t luts = Lut::Manager::Shared().Rescan();
        if (luts > 0) {
            SetStatus("LUT manager: found " + std::to_string(luts) +
                      " LUT(s)");
        }
    }

    // OCIO looks/transforms browser (ui/color_panel_widget.h): "Add
    // to Clip" emits "ocio:<name>" which lands as a ClipEffect on the
    // current selection — same path as the plugins panel. Refresh()
    // re-reads the profiles Boot() loaded from assets/color.yaml plus
    // the LUT manager's scanned looks.
    color_panel_ = ColorPanelWidget::Create();
    color_panel_->SetOnAddEffect([this](const std::string& type) {
        ClipEffect fx;
        fx.type = type;
        AddClipEffectBy(selected_clip_id_, fx);
    });
    color_panel_->Refresh();

    // Embedded Chromium tab (src/cef): graceful placeholder when the
    // CEF binary distribution is not installed next to the exe.
    browser_panel_ = std::make_shared<BrowserPanelWidget>(parent_hwnd_);

    // VST3 editor popup (ui/vst3_editor.h): when its session ends,
    // commit the edited parameters back into the bound clip's fx
    // node — the mixer chain re-syncs and the project file keeps them.
    vst3_editor_.SetOnClosed([this] { CommitVst3EditorParams(); });

    // .xexrj extension host: verified TZ archives under
    // assets/extensions/ whose main.js runs in the shared V8 engine;
    // extension UI renders via xer-ext:// inside the Web tab.
    ExtHost::Shared().SetActionFn(
        [this](const std::string& action, const std::string& arg_json) {
            return DispatchExtensionAction(action, arg_json);
        });
    ExtHost::Shared().SetOpenUiFn([this](const std::string& url) {
        right_panel_visible_ = true;
        SetRightTab(9);
        if (browser_panel_) browser_panel_->OpenUrl(url);
        RequestLayout();
        Invalidate();
    });
    {
        const auto ext_dir =
            AppRuntime::SharedAssetsConfig().assets_dir / "extensions";
        if (!ext_dir.empty()) {
            ExtHost::Shared().Scan(ext_dir.string());
            const auto& exts = ExtHost::Shared().Entries();
            if (!exts.empty()) {
                size_t good = 0, ran = 0;
                for (const auto& e : exts) {
                    good += e.verified;
                    // Verified extensions auto-run their entry point —
                    // that's the "main.js executes inside the TZ"
                    // contract; failures land on the status bar.
                    if (!e.verified) continue;
                    auto r = ExtHost::Shared().Run(e.manifest.id);
                    if (r.ok) {
                        ++ran;
                    } else {
                        SetStatus("ext '" + e.manifest.id +
                                  "': " + r.error, true);
                    }
                }
                const std::string msg =
                    "extensions: " + std::to_string(good) + "/" +
                    std::to_string(exts.size()) + " verified, " +
                    std::to_string(ran) + " running";
                std::cerr << msg << "\n";   // visible in headless logs
                SetStatus(msg);
            }
        }
    }

    TimelineWidget::Hooks timeline_hooks;
    timeline_hooks.on_select_clip = [this](const std::string& id) {
        SelectClip(id);
    };
    timeline_hooks.on_seek = [this](double t) {
        monitor_target_ = MonitorTarget::Program;
        Seek(t);
    };
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
    timeline_hooks.on_add_transition =
        [this](const std::string& id, Transition::Type type,
               double duration, const std::string& script) {
            AddTransitionAfter(id, type, duration, script);
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
    // Empty-lane "Add … Here" context-menu items: mattes configure
    // through the New Layer dialog first (the lane/time rides
    // pending_layer_*); bars/text land immediately.
    timeline_hooks.on_add_generated =
        [this](int track, SourceKind kind, double at_seconds) {
            if (kind == SourceKind::ColorMatte) {
                ShowNewLayerDialog(track, at_seconds);
            } else {
                AddGeneratedClipAt(kind, track, at_seconds, "", "",
                                   5.0);
                // Same flow as the menu: a new title opens the
                // existing title editor on the just-selected clip.
                if (kind == SourceKind::Text) OpenTitleEditor();
            }
        };
    timeline_hooks.on_add_media = [this](int track, double at_seconds) {
        // Bin-entry insertion at click time: a selected bin item
        // lands directly on the clicked lane/time; nothing selected
        // falls back to the disk picker.
        if (FindCatalogEntry(selected_media_path_) != nullptr) {
            AddMediaToTimelineAt(selected_media_path_, at_seconds,
                                 track);
        } else {
            ShowAddMediaDialog(track, at_seconds);
        }
    };
    // Non-blocking lookups into the media-prep worker — a cold decode
    // answers kPending and lands on a later frame (ConsumeRenderDirty
    // in Tick drives the repaint).
    timeline_hooks.get_audio_peaks =
        [this](const std::string& path, AudioPeaks* out) {
            return ToUiLookup(media_prep_.LookupPeaks(path, out));
        };
    timeline_hooks.get_poster =
        [this](const std::string& path, double src_t,
               video::Frame* frame) {
            return ToUiLookup(
                media_prep_.LookupPoster(path, src_t, frame));
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
    AddChild(sync_cancel_btn_);
    AddChild(zoom_label_);
    AddChild(nav_rail_);
    AddChild(media_bin_);
    AddChild(monitor_);
    AddChild(source_monitor_);
    AddChild(inspector_);
    AddChild(effects_panel_);
    AddChild(markers_panel_);
    AddChild(lab_panel_);
    AddChild(history_panel_);
    AddChild(mixer_panel_);
    AddChild(export_queue_);
    AddChild(plugins_panel_);
    AddChild(color_panel_);
    AddChild(browser_panel_);
    AddChild(layers_panel_);
    AddChild(waveform_panel_);
    AddChild(status_toast_);
    AddChild(tab_strip_);
    AddChild(timeline_);
}

bool VideoEditorAppWidget::OnContextMenu(
    const XER::UI::Widgets::Event& event) {
    const XER::UI::Layout::Point p = event.position;
    if (timeline_ != nullptr && timeline_->IsVisible() &&
        timeline_->ShowContextMenu(p)) {
        return true;
    }
    if (media_bin_ != nullptr && media_bin_->IsVisible() &&
        media_bin_->ShowContextMenu(p)) {
        return true;
    }
    if (monitor_ != nullptr && monitor_->IsVisible() &&
        monitor_->ShowContextMenu(p)) {
        return true;
    }
    return false;
}

void VideoEditorAppWidget::InstallGlobalShortcuts() {
    if (shortcuts_installed_ || GetTree() == nullptr) return;
    shortcuts_installed_ = true;
    GetTree()->SetKeyHandler([this](const Widgets::Event& event) {
        return HandleEvent(event);
    });
}

void VideoEditorAppWidget::BuildTitleBar() {
    using MenuItem = Widgets::AppTitleBar::MenuItem;
    title_bar_ = std::make_shared<Widgets::AppTitleBar>(
        AppRuntime::SharedUiStrings().Get("app_name", "XER Studio"));
    title_bar_->SetWorkspaceLabel("Video Editor");
    // The center pill carries the project file name; activating it
    // opens the project-settings dialog (rename/settings in one spot).
    title_bar_->SetCenterCommand(
        "Untitled.xerproj",
        [this] { ShowProjectSettingsDialog(); }, false);
    // Workspace-label dropdown: the same BuiltinWorkspaces presets the
    // View menu lists, routed to ApplyWorkspace.
    std::vector<MenuItem> workspace_items;
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        workspace_items.push_back(
            {ws.name, "", [this, ws] { ApplyWorkspace(ws); }});
    }
    title_bar_->SetWorkspaceItems(std::move(workspace_items));
    // Avatar chip: no account system in the editor, so it doubles as
    // a quick shortcut menu instead of a stub "U" glyph.
    title_bar_->SetAvatar(
        "Editor",
        {{"Project Settings…", "",
          [this] { ShowProjectSettingsDialog(); }},
         {"Keyboard Shortcuts", "?",
          [this] { ToggleShortcutsOverlay(); }}});
    RebuildTitleBarMenus();
    // Export lives in the main toolbar (pinned right) — a second pill
    // in the title bar duplicated it.
    title_bar_->SetLeftPanelToggle(true, left_panel_visible_, [this] {
        left_panel_visible_ = !left_panel_visible_;
        RequestLayout();
    });
    title_bar_->SetRightPanelToggle(true, right_panel_visible_, [this] {
        right_panel_visible_ = !right_panel_visible_;
        RequestLayout();
    });
}

void VideoEditorAppWidget::RebuildTitleBarMenus() {
    if (!title_bar_) return;
    using Menu = Widgets::AppTitleBar::Menu;
    using MenuItem = Widgets::AppTitleBar::MenuItem;

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
        item("Recover Autosave", "", [this] { RecoverAutosave(); },
             autosave_offer_),
        item("Save", "Ctrl+S", [this] { SaveProject(false); }),
        item("Save As…", "Ctrl+Shift+S",
             [this] { SaveProject(true); }),
        item("Import Media…", "", [this] { ImportMedia(); }, true, false,
             false, true),
        item("Import from URL…", "", [this] { ImportFromUrl(); }),
        item("Project Settings…", "",
             [this] { ShowProjectSettingsDialog(); }, true, false,
             false, true),
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
             [this] { OpenVst3FxEditor(selected_clip_id_, -1); }),
    };
    // JS audio effects (assets/scripts/fx): one menu item each —
    // they append to the same clip.audio_fx list as VST3 entries and
    // route to the JsFxRuntime at mix time.
    for (const auto& d : JsFxRuntime::AudioEffects()) {
        const std::string id = d.id;
        edit.items.push_back(
            item("Add JS FX: " + d.title, "",
                 [this, id] { AddScriptAudioFxToSelectedClip(id); }));
    }
    Menu tools;
    tools.label = "Tools";
    tools.items = {
        item("Fonts\u2026", "", [this] { OpenFontManager(); }),
    };
    Menu timeline;
    timeline.label = "Timeline";
    const Clip* menu_sel_clip = FindClip(project_, selected_clip_id_);
    timeline.items = {
        item("Add Video Track", "", [this] { AddTrack(TrackKind::Video); }),
        item("Add Audio Track", "", [this] { AddTrack(TrackKind::Audio); }),
        item("Add Color Matte…", "",
             [this] { ShowNewLayerDialog(-1, -1.0); }, true, false,
             false, true),
        item("Add Color Bars", "",
             [this] { AddGeneratedClip(SourceKind::ColorBars); }),
        item("Add Text…", "", [this] {
            AddGeneratedClip(SourceKind::Text);
            OpenTitleEditor();
        }),
        item("Add Media…", "",
             [this] { ShowAddMediaDialog(-1, playhead_); }),
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
        item("Sync Audio by Waveform", "",
             [this] { SyncAudioClips(); }),
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
        item("Source Monitor", "", [this] {
            source_visible_ = !source_visible_;
            RequestLayout();
        }, true, source_visible_),
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
        item("Color", "", [this] { SetRightTab(8); }, true,
             right_tab_ == 8),
        item("Browser", "", [this] {
            right_panel_visible_ = true;
            SetRightTab(9);
        }, true, right_tab_ == 9),
        item("Layers", "", [this] {
            right_panel_visible_ = true;
            SetRightTab(10);
        }, true, right_tab_ == 10),
        item("Keyboard Shortcuts", "?", [this] {
            ToggleShortcutsOverlay();
        }),
    };
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        view.items.push_back(
            item("Workspace: " + ws.name, "",
                 [this, ws] { ApplyWorkspace(ws); }));
    }
    title_bar_->SetMenus({file, edit, tools, timeline, view});
}

void VideoEditorAppWidget::ShowProjectSettingsDialog() {
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
}

void VideoEditorAppWidget::NewProject() {
    if (export_engine_.IsRunning()) {
        SetStatus("Wait for export to finish", true);
        return;
    }
    if (!ConfirmDiscardChanges()) return;
    history_.Clear();
    media_prep_.CancelAllQueued();   // project swap: drop stale decodes
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
    // No project dir → drop that LUT search dir and re-scan.
    Lut::Manager::Shared().SetProjectDir({});
    Lut::Manager::Shared().Rescan();
    if (color_panel_) color_panel_->Refresh();
    NotifyProjectChanged();
    project_dirty_ = false;   // fresh canvas — nothing to lose
    SetStatus("New project");
}

bool VideoEditorAppWidget::LoadProject(const std::string& utf8_path) {
    std::string error;
    history_.Clear();
    media_prep_.CancelAllQueued();   // project swap: drop stale decodes
    if (!ProjectStore::Load(Path::NormalizeUtf8Path(utf8_path), &project_,
                            &error)) {
        SetStatus("Open failed: " + error, true);
        return false;
    }
    project_path_ = Path::NormalizeUtf8Path(utf8_path);
    InitAutosave();
    selected_clip_id_.clear();
    playhead_ = 0.0;
    // <project>/luts joins the LUT search dirs — re-scan so project
    // LUTs resolve their ocio:<name> effects.
    Lut::Manager::Shared().SetProjectDir(
        Path::FromUtf8(project_path_).parent_path());
    Lut::Manager::Shared().Rescan();
    if (color_panel_) color_panel_->Refresh();
    NotifyProjectChanged();
    project_dirty_ = false;   // just loaded — matches the file on disk
    SetStatus("Opened " + Basename(project_path_));
    return true;
}

void VideoEditorAppWidget::OpenProject() {
    // Confirm before the picker so a cancelled prompt never strands a
    // file dialog for an action the user just aborted.
    if (!ConfirmDiscardChanges()) return;
#ifdef _WIN32
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"XER project", L"*.xerproj"), L"Open project");
    if (!picked.empty()) LoadProject(Str::WideToUtf8(picked));
#else
    SetStatus("Open dialog is Windows-only", true);
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
    // A save/rename moves the project dir — re-scan its luts dir.
    Lut::Manager::Shared().SetProjectDir(
        Path::FromUtf8(project_path_).parent_path());
    Lut::Manager::Shared().Rescan();
    if (color_panel_) color_panel_->Refresh();
    InitAutosave();   // bind to a first-saved (or renamed) path
    autosave_.ClearDirty();
    project_dirty_ = false;
    if (title_bar_) title_bar_->SetCenterCommandValue(Basename(project_path_));
    return true;
}

// The one place the editor asks "discard work?" — New/Open/window-
// close all funnel here so the prompt behaves identically.
bool VideoEditorAppWidget::ConfirmDiscardChanges() {
    if (!project_dirty_) return true;
#ifdef _WIN32
    const std::wstring name = Str::Utf8ToWide(
        project_path_.empty() ? "Untitled.xerproj"
                              : Basename(project_path_));
    const int choice = MessageBoxW(
        static_cast<HWND>(parent_hwnd_),
        (L"Save changes to \"" + name + L"\"?").c_str(),
        L"Unsaved changes", MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) {
        // A cancelled or failed save aborts the destructive op too —
        // otherwise "yes, save" would still lose the work.
        return SaveProject(false);
    }
    return choice == IDNO;
#else
    // Native prompts are Windows-only (same gating as the file
    // pickers) — proceed rather than trapping the user.
    return true;
#endif
}

void VideoEditorAppWidget::ImportMedia() {
#ifdef _WIN32
    // The curated list covers the common FFmpeg demuxers + OIIO
    // stills, but classification comes from the probe (video_info.cc /
    // MediaCatalog::Import), never the extension — FilterSpec appends
    // an "All files (*.*)" second choice so any file can be picked and
    // still lands on the right track kind.
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"Media", MediaImportPattern().c_str()),
        L"Import media");
    if (picked.empty()) return;
    const std::string path =
        Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
    std::string error;
    if (!ImportMediaFile(path, &error)) {
        SetStatus("Import failed: " + error, true);
        return;
    }
    RefreshCatalog();
    const CatalogEntry* imported = FindCatalogEntry(path);
    media_prep_.RequestImportPoster(imported != nullptr ? imported->path
                                                        : path);
    SetStatus("Imported " + Basename(path));
#else
    SetStatus("Import dialog is Windows-only", true);
#endif
}

bool VideoEditorAppWidget::ImportMediaFile(const std::string& path,
                                           std::string* error) {
    std::int64_t id = 0;
    // extract_poster=false — the poster PNG decode lands on the
    // media-prep worker; ConsumeCatalogDirty in Tick refreshes the bin
    // once poster_path is written.
    if (!catalog_.Open(error) ||
        !catalog_.Import(path, &id, false, error)) {
        return false;
    }
    selected_media_path_ = path;
    return true;
}

void VideoEditorAppWidget::OnOsFilesDropped(
    const std::vector<std::wstring>& wide_paths) {
    // Expand top-level directory contents (non-recursive v1) and keep
    // only paths matching the shared import extension list — the
    // catalog probe would reject the rest anyway, but filtering here
    // keeps junk drops (.txt/.exe/.xerproj) from queueing probe work.
    std::vector<std::string> media;
    int skipped = 0;
    const auto add_path = [&](const std::filesystem::path& p) {
        const std::string utf8 =
            Path::NormalizeUtf8Path(Path::ToUtf8(p));
        if (IsMediaImportPath(utf8)) {
            media.push_back(utf8);
        } else {
            ++skipped;
        }
    };
    for (const auto& wpath : wide_paths) {
        if (wpath.empty()) continue;
        std::error_code ec;
        const std::filesystem::path p(wpath);
        if (std::filesystem::is_directory(p, ec)) {
            for (const auto& de :
                 std::filesystem::directory_iterator(p, ec)) {
                if (de.is_regular_file(ec)) add_path(de.path());
            }
        } else if (std::filesystem::is_regular_file(p, ec)) {
            add_path(p);
        } else {
            ++skipped;
        }
    }
    ImportDroppedPaths(media, skipped);
}

void VideoEditorAppWidget::ImportDroppedPaths(
    const std::vector<std::string>& paths, int skipped) {
    if (paths.empty()) {
        SetStatus(skipped > 0 ? "Drop ignored — no importable media files"
                              : "Drop ignored — nothing to import",
                  true);
        return;
    }
    if (paths.size() > 1) {
        SetStatus("Importing " + std::to_string(paths.size()) +
                  " files…");
    }
    int imported = 0;
    std::string last_error;
    for (const auto& path : paths) {
        std::string error;
        if (ImportMediaFile(path, &error)) {
            ++imported;
        } else {
            ++skipped;
            if (!error.empty()) last_error = error;
        }
    }
    RefreshCatalog();
    for (const auto& path : paths) {
        const CatalogEntry* entry = FindCatalogEntry(path);
        if (entry != nullptr) {
            media_prep_.RequestImportPoster(entry->path);
        }
    }
    if (paths.size() == 1 && imported == 1) {
        SetStatus("Imported " + Basename(paths.front()));
        return;
    }
    std::string msg =
        "Imported " + std::to_string(imported) + ", skipped " +
        std::to_string(skipped);
    if (imported == 0 && !last_error.empty()) {
        msg += " — " + last_error;
    }
    SetStatus(msg, imported == 0);
}

void VideoEditorAppWidget::ImportFromUrl() {
    if (url_dialog_ == nullptr) return;
    url_dialog_->Reset();
    ShowOverlay(url_dialog_);
}

void VideoEditorAppWidget::StartUrlImport(const std::string& url) {
    if (url_op_.IsRunning() || url_op_.HasResult()) {
        SetStatus("A URL import is already running", true);
        return;
    }
    {
        std::lock_guard lock(url_result_mutex_);
        url_result_ = UrlImport::Result{};
        url_status_.clear();
    }
    url_progress_shown_ = -1.0f;
    SetStatus("Downloading " + url + "…");
    const bool started = url_op_.Start(
        [this, url](const Lab::JobControl& ctl) {
            const auto progress =
                [this, &ctl](float fraction,
                             const std::string& status) {
                    {
                        std::lock_guard lock(url_result_mutex_);
                        url_status_ = status;
                    }
                    if (fraction >= 0.0f) ctl.ReportProgress(fraction);
                };
            UrlImport::Result result = UrlImport::Download(
                url, /*dest_dir*/ "", progress,
                [&ctl] { return ctl.ShouldCancel(); });
            {
                std::lock_guard lock(url_result_mutex_);
                url_result_ = result;
            }
            if (!result.ok) {
                ctl.Fail(result.cancelled ? "cancelled" : result.error);
            }
            return result.ok;
        });
    if (!started) {
        SetStatus("A URL import is already running", true);
    }
}

void VideoEditorAppWidget::AddSelectedToTimeline() {
    AddMediaToTimeline(selected_media_path_);
}

void VideoEditorAppWidget::AddMediaToTimeline(const std::string& path) {
    AddMediaToTimelineAt(path, playhead_);
}

void VideoEditorAppWidget::AddMediaToTimelineAt(const std::string& path,
                                              double at_seconds,
                                              int preferred_track) {
    // The media-bin context menu passes the clicked row's path; the
    // toolbar Add button passes the selection. Fall back to the
    // selection only when the explicit path isn't in the catalog.
    const CatalogEntry* entry = FindCatalogEntry(path);
    if (entry == nullptr && path != selected_media_path_) {
        entry = FindCatalogEntry(selected_media_path_);
    }
    if (entry == nullptr) {
        SetStatus("Select media in the bin first", true);
        return;
    }
    // Classification (documented on CatalogEntry::HasVideo): any
    // imported file is exactly one of picture-bearing -> Video track
    // or audio-only -> Audio track. Import guarantees has_audio on
    // non-video rows, so there is no third state to handle. When no
    // track of the wanted kind exists yet, one is created below —
    // audio lands on Audio even on a video-only timeline and vice
    // versa.
    const TrackKind wanted =
        entry->HasVideo() ? TrackKind::Video : TrackKind::Audio;
    // Premiere-style A/V placement: a video file that ALSO carries an
    // audio stream lands as a linked pair — a video clip on a video
    // track plus an audio clip (same source, same range) on an audio
    // track, bound by a shared link_group. The mixer suppresses the
    // video clip's embedded audio in favor of the linked clip
    // (audio_mixer.cc), so the stream is decoded exactly once.
    const bool linked_av = entry->HasVideo() && entry->HasAudio();
    auto first_unlocked = [this](TrackKind kind) {
        for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
            // Skip locked tracks — same placement rule the source
            // monitor's three-point edits use.
            if (project_.tracks[i].kind == kind &&
                !project_.tracks[i].locked) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    history_.BeginMacro("Add clip");
    bool applied_any = false;
    bool ok = true;
    std::string error;
    std::string clip_id;
    std::string link_group;

    // A bin drop on an empty lane names its track explicitly; honor
    // it when kind+lock allow, else fall back to the first free lane
    // of the wanted kind (the historical behavior).
    int track_index = -1;
    if (preferred_track >= 0 &&
        preferred_track < static_cast<int>(project_.tracks.size()) &&
        project_.tracks[preferred_track].kind == wanted &&
        !project_.tracks[preferred_track].locked) {
        track_index = preferred_track;
    } else {
        track_index = first_unlocked(wanted);
    }
    if (track_index < 0) {
        track_index = history_.AddTrack(
            wanted, wanted == TrackKind::Video ? "Video" : "Audio");
        applied_any |= track_index >= 0;
        if (track_index < 0) {
            ok = false;
            error = "unable to add track";
        }
    }
    int audio_track = -1;
    if (ok && linked_av) {
        audio_track = first_unlocked(TrackKind::Audio);
        if (audio_track < 0) {
            audio_track = history_.AddTrack(TrackKind::Audio, "Audio");
            applied_any |= audio_track >= 0;
            if (audio_track < 0) {
                ok = false;
                error = "unable to add audio track";
            }
        }
    }
    double clip_start = std::max(0.0, at_seconds);
    double clip_duration = 0.0;
    if (ok) {
        clip_start = snap_enabled_
            ? SnapTime(project_, track_index, clip_start, 0.1, playhead_)
            : clip_start;
        clip_duration = entry->duration_seconds > 0.0
            ? entry->duration_seconds
            : 5.0;
        Clip clip;
        clip.id = NewClipId();
        clip.source_path = entry->path;
        clip.track = track_index;
        clip.timeline_start = clip_start;
        clip.in_point = 0.0;
        clip.duration = clip_duration;
        if (entry->HasVideo()) {
            clip.transform = FitTransformToCanvas(
                entry->width, entry->height,
                project_.width, project_.height);
        }
        if (linked_av) {
            link_group = NewClipId();
            clip.link_group = link_group;
        }
        MediaBounds bounds;
        bounds.duration_seconds = entry->duration_seconds;
        bounds.has_video = entry->HasVideo();
        ok = history_.AddClip(std::move(clip), bounds, &clip_id,
                              &error);
        applied_any |= ok;
    }
    if (ok && linked_av) {
        Clip audio_clip;
        audio_clip.id = NewClipId();
        audio_clip.source_path = entry->path;
        audio_clip.track = audio_track;
        audio_clip.timeline_start = clip_start;
        audio_clip.in_point = 0.0;
        audio_clip.duration = clip_duration;
        audio_clip.link_group = link_group;
        // bounds.has_video stays false — this clip is the audio leg
        // of the pair; validation accepts non-video media on audio
        // tracks.
        MediaBounds audio_bounds;
        audio_bounds.duration_seconds = entry->duration_seconds;
        std::string audio_id;
        ok = history_.AddClip(std::move(audio_clip), audio_bounds,
                              &audio_id, &error);
        applied_any |= ok;
    }
    history_.EndMacro();
    if (!ok) {
        if (applied_any) {
            history_.Undo();   // rolls the whole macro back
        }
        SetStatus("Add clip failed: " + error, true);
        return;
    }
    SelectClip(clip_id);
    NotifyProjectChanged();
    SetStatus("Added " + Basename(entry->path));
}

void VideoEditorAppWidget::OnBinMediaDrop(
    const std::string& path, XER::UI::Layout::Point position) {
    OnBinDragHover(position, false);   // always clear the affordance
    if (timeline_ == nullptr ||
        !timeline_->GetBounds().Contains(position)) {
        return;   // released somewhere else — not a timeline drop
    }
    AddMediaToTimelineAt(path, timeline_->TimeAtDropPoint(position),
                         timeline_->TrackAtDropPoint(position));
}

void VideoEditorAppWidget::OnBinDragHover(
    XER::UI::Layout::Point position, bool active) {
    const bool over = active && timeline_ != nullptr &&
                      timeline_->GetBounds().Contains(position);
    if (timeline_ != nullptr && over != bin_drop_hovering_) {
        bin_drop_hovering_ = over;
        timeline_->SetExternalDropHover(over);
        if (over) {
            SetStatus("Drop to add media at " +
                      FormatTimecode(
                          timeline_->TimeAtDropPoint(position),
                          project_.fps_num, project_.fps_den));
        }
    }
    if (!active) bin_drop_hovering_ = false;
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
    // Mixer master fader -> export mix bus (ExportOptions field added
    // by the engine pass).
    options.master_gain_db = project_.master_gain_db;
    EnqueueExport(options);
}

void VideoEditorAppWidget::EnqueueExport(const ExportOptions& options) {
    std::string error;
    if (!export_engine_.Start(project_, options, {}, &error)) {
        SetStatus("Export failed: " + error, true);
        return;
    }
    if (export_dialog_) {
        // Reset re-arms the finished flag so Tick() lands the
        // completion — a retried job never re-opens the dialog.
        export_dialog_->Reset();
        export_dialog_->SetProgress(export_engine_.Progress());
    }
    // Track the job in the export queue panel (cap the history at 8).
    ExportJobRow job;
    job.label = Basename(options.output_path);
    job.output_path = options.output_path;
    // Empty video_codec is the audio-only convention (export_preset.cc)
    // — don't print a leading " 0 kbps" video column for those jobs.
    job.detail = options.video_codec.empty()
                     ? options.audio_codec + " " +
                           std::to_string(options.audio_bit_rate / 1000) +
                           " kbps · audio only"
                     : options.video_codec + " " +
                           std::to_string(options.video_bit_rate / 1000) +
                           " kbps · " + options.audio_codec;
    job.progress = 0.0f;
    job.state = static_cast<int>(ExportJobState::Running);
    export_jobs_.push_back(std::move(job));
    export_job_options_.push_back(options);
    while (export_jobs_.size() > 8) {
        export_jobs_.erase(export_jobs_.begin());
        export_job_options_.erase(export_job_options_.begin());
    }
    RefreshExportQueue();
    SetStatus("Exporting " + Basename(options.output_path));
}

void VideoEditorAppWidget::RetryExport(int job) {
    if (job < 0 || job >= static_cast<int>(export_jobs_.size()) ||
        job >= static_cast<int>(export_job_options_.size())) {
        return;
    }
    if (export_engine_.IsRunning()) {
        SetStatus("Export already running", true);
        return;
    }
    // Re-issue with the options captured at queue time. The failed row
    // is dropped so the retried job lands at the back — Tick() tracks
    // progress on export_jobs_.back() only.
    const ExportOptions options = export_job_options_[job];
    export_jobs_.erase(export_jobs_.begin() + job);
    export_job_options_.erase(export_job_options_.begin() + job);
    EnqueueExport(options);
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
    monitor_target_ = MonitorTarget::Program;
    selected_clip_id_ = clip_id;
    timeline_->SetSelectedClip(clip_id);
    inspector_->SetClip(FindClip(project_, clip_id));
    if (layers_panel_) {
        // Selecting a clip switches the panel's active track to the
        // clip's own lane, mirroring the inspector.
        layers_panel_->SetActiveTrack(TargetVideoTrack());
        layers_panel_->SetSelectedClip(clip_id);
    }
    // Timeline menu items ("Edit Title…", "Speed/Duration…") snapshot
    // the selection at build time — refresh so they track it.
    RebuildTitleBarMenus();
    SetStatus(clip_id.empty() ? "Selection cleared"
                              : "Selected " + clip_id);
}

void VideoEditorAppWidget::Seek(double seconds) {
    monitor_target_ = MonitorTarget::Program;
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

bool VideoEditorAppWidget::PrepareUiCapture(double seconds,
                                            int timeout_ms) {
    // Same lazy init the play path uses; a headless machine without
    // an audio endpoint still inits (the sink is optional — chunks are
    // mixed for the meters, just not written).
    EnsurePreviewEngine();
    if (!preview_ready_) {
        return false;
    }
    Seek(seconds);                     // widget-level playhead fan-out
    preview_engine_.Seek(playhead_);   // paused seek presents a frame
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, timeout_ms));
    while (preview_engine_.FramesPresented() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        Tick(1.0 / 30.0);  // marshal worker callbacks into widgets
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    Tick(1.0 / 30.0);  // one last marshal for the just-presented frame
    return preview_engine_.FramesPresented() > 0;
}

void VideoEditorAppWidget::StopProgramTransport() {
    shuttle_rate_ = 0.0;
    if (!monitor_->IsPlaying()) return;
    preview_engine_.SetRate(1.0);
    preview_engine_.SetPlaying(false);
    monitor_->SetPlaying(false);
    monitor_->SetExternalPlayback(false);
    monitor_->SetTime(playhead_, true);  // still at the stop point
    play_btn_->SetText("Play");
}

void VideoEditorAppWidget::TogglePlay() {
    monitor_target_ = MonitorTarget::Program;
    const bool next = !monitor_->IsPlaying();
    // Space always returns to normal 1x playback — any JKL ladder
    // state is dropped (K also resets it).
    shuttle_rate_ = 0.0;
    if (next) {
        // Exclusive transport: source monitor playback yields to the
        // program monitor (the two engines share the audio sink).
        StopSourceTransport();
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
        preview_engine_.SetRate(1.0);
        preview_engine_.SetPlaying(true);
        monitor_->SetExternalPlayback(true);
        monitor_->SetPlaying(true);
    } else {
        StopProgramTransport();
    }
    play_btn_->SetText(next ? "Pause" : "Play");
}

void VideoEditorAppWidget::EnsurePlatformAudioSink(
    std::unique_ptr<IAudioSink>* which) {
    if (*which) return;
    // A failed init previously latched the null sink for the whole
    // session — devices that appear later (headphones, USB DAC,
    // endpoint renegotiation after WASAPI shared-mode fixes) could
    // never recover. Retry throttled; the first failure reports once.
    const auto now = std::chrono::steady_clock::now();
    if (audio_sink_tried_ && now < audio_sink_retry_at_) {
        return;
    }
    audio_sink_tried_ = true;
    audio_sink_retry_at_ = now + std::chrono::seconds(3);
    std::string sink_error;
    *which = MakePlatformAudioSink(&sink_error);
    if (*which) {
        // Late bind: engines already initialized keep their forwarding
        // sink — point it at the device that just came up.
        (which == &program_audio_sink_ ? program_sink_binding_
                                       : source_sink_binding_)
            .Bind(which->get());
    } else if (!sink_error.empty()) {
        SetStatus("Audio preview unavailable: " + sink_error);
    }
}

void VideoEditorAppWidget::EnsurePreviewEngine() {
    if (preview_ready_ && !preview_dirty_) return;
    preview_engine_.Shutdown();
    preview_ready_ = false;
    if (!engine_sink_) {
        engine_sink_ = std::make_unique<EngineSink>(this);
    }
    EnsurePlatformAudioSink(&program_audio_sink_);
    PreviewOptions options;
    options.audio_sink = &program_sink_binding_;
    std::string error;
    preview_ready_ = preview_engine_.Init(project_, options,
                                        engine_sink_.get(), &error);
    // Init resets the master-gain command — re-apply the UI fader.
    if (preview_ready_) {
        preview_engine_.SetMasterGainDb(project_.master_gain_db);
        // Same for the monitor's playback-quality pick — Init resets
        // the engine's quality command to full res.
        preview_engine_.SetQuality(preview_quality_);
    }
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

void VideoEditorAppWidget::SyncAudioClips() {
    // The whole plan (project read + O(n·m) correlation + peak
    // decodes) runs on sync_op_ — never on the UI thread. A multi-cam
    // sync decodes every take's peaks and can take tens of seconds;
    // the old in-line poll loop froze the message pump for all of it
    // AND made a cancel button impossible (clicks need a live pump).
    // The job reads a project SNAPSHOT so timeline edits during the
    // run can't race it; Tick() pops the finished plan and applies
    // the moves here on the UI thread. Cancel affordance: the
    // status-strip Cancel button (or Esc) -> RequestSyncCancel().
    if (sync_op_.IsRunning() || sync_op_.HasResult()) {
        SetStatus("Audio sync already running", true);
        return;
    }
    Project snapshot = project_;
    // Selected clip is the reference when one is picked; otherwise the
    // earliest clip on the lowest track anchors the sync.
    const std::string reference = selected_clip_id_;
    sync_cancel_.store(false, std::memory_order_relaxed);
    sync_progress_done_.store(0, std::memory_order_relaxed);
    sync_progress_total_.store(0, std::memory_order_relaxed);
    sync_progress_shown_ = -1;
    {
        std::lock_guard lock(sync_result_mutex_);
        sync_report_ = AudioSyncReport{};
    }
    if (sync_cancel_btn_ != nullptr) {
        sync_cancel_btn_->SetVisible(true);
        Invalidate();
    }
    SetStatus("Syncing audio…");
    const bool started = sync_op_.Start(
        [this, snapshot = std::move(snapshot),
         reference](const Lab::JobControl& ctl) {
            AudioSyncPlanOptions options;
            options.cancel = &sync_cancel_;
            options.progress = [&](int done, int total) {
                sync_progress_done_.store(done,
                                          std::memory_order_relaxed);
                sync_progress_total_.store(total,
                                           std::memory_order_relaxed);
            };
            // In-process fallback path: peak decodes go through
            // media_prep_ — worker pool, subprocess isolation, disk
            // cache — instead of a direct DecodeAudioPeaks call:
            // LookupPeaks is lookup-or-enqueue, so the provider below
            // is a bounded, cancel-aware wait that also gives repeat
            // syncs instant disk-cache hits.
            options.peaks = [&](const std::string& path,
                                AudioPeaks* out) -> bool {
                const auto deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(120);
                for (;;) {
                    const MediaPrep::Lookup state =
                        media_prep_.LookupPeaks(path, out);
                    if (state == MediaPrep::Lookup::kReady) return true;
                    if (state == MediaPrep::Lookup::kFailed) {
                        return false;
                    }
                    if (sync_cancel_.load(std::memory_order_relaxed) ||
                        std::chrono::steady_clock::now() >= deadline) {
                        return false;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(20));
                }
            };
            AudioSyncReport report;
            std::string error;
            bool ok = false;
            // XERSyncMedia.exe first — the dedicated engine subprocess
            // does its own peak decode + correlation, so heavy
            // multi-cam syncs (and decoder faults) live entirely off
            // this process. Spawn/transport failure falls back to the
            // in-process plan; kFailed keeps `error` — no fallback.
#if defined(_WIN32)
            const AudioSyncRoute route = ComputeAudioSyncPlanViaWorker(
                snapshot, reference, &report, &error, &sync_cancel_);
            if (route == AudioSyncRoute::kPlanned) {
                ok = true;
            } else if (route == AudioSyncRoute::kUnavailable) {
                // Worker absent/dead — the same plan runs in-process.
                ok = ComputeAudioSyncPlan(snapshot, reference, &report,
                                          &error, options);
            }
#else
            ok = ComputeAudioSyncPlan(snapshot, reference, &report,
                                      &error, options);
#endif
            {
                std::lock_guard lock(sync_result_mutex_);
                sync_report_ = std::move(report);
            }
            if (!ok) {
                ctl.Fail(error.empty() ? std::string("sync failed")
                                       : std::move(error));
            }
            return ok;
        });
    if (!started) {
        if (sync_cancel_btn_ != nullptr) {
            sync_cancel_btn_->SetVisible(false);
            Invalidate();
        }
        SetStatus("Audio sync could not start — a task is finishing",
                  true);
    }
}

void VideoEditorAppWidget::RequestSyncCancel() {
    if (!sync_op_.IsRunning()) return;
    sync_cancel_.store(true, std::memory_order_relaxed);
    sync_op_.RequestCancel();   // PopResult reports cancelled=true
    // A remote plan can't be polled mid-request — the worker is
    // job-grouped and disposable, so cancel = kill it. No death
    // penalty; the next sync respawns it. Harmless when the
    // in-process path owns the run (the flag above aborts it).
    SyncMediaPipe().KillWorker();
    SetStatus("Cancelling audio sync…");
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
    // Grab the source path before the clip leaves the model so its
    // queued media-prep requests can be dropped (still-needed media
    // simply re-enqueues on the next render).
    const Clip* doomed = FindClip(project_, clip_id);
    const std::string source =
        doomed != nullptr ? doomed->source_path : std::string{};
    if (!history_.RemoveClip(clip_id)) {
        SetStatus("Delete failed", true);
        return;
    }
    // A plugin editor bound to the doomed clip closes with it — the
    // commit-back inside Hide() no-ops once the clip is gone.
    if (vst3_editor_clip_ == clip_id) {
        vst3_editor_.Hide();
    }
    if (!source.empty()) media_prep_.CancelQueued(source);
    selected_clip_id_.clear();
    NotifyProjectChanged();
    SetStatus("Deleted clip");
}

void VideoEditorAppWidget::AddTransitionAfter(
    const std::string& clip_id, Transition::Type type,
    double duration, const std::string& script) {
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
    AddTransitionPair(clip->id, next->id, type, duration, script);
}

void VideoEditorAppWidget::AddTransitionPair(
    const std::string& from_id, const std::string& to_id,
    Transition::Type type, double duration,
    const std::string& script) {
    const double applied =
        duration > 0.0 ? duration : transition_duration_;
    std::string error;
    if (!history_.AddTransition(from_id, to_id, type, applied,
                                &error, script)) {
        SetStatus("Transition failed: " + error, true);
        return;
    }
    // The applied pick re-arms the default — Ctrl+D and the junction
    // menu's <= 0 duration requests follow the last-used pair.
    transition_type_ = type;
    transition_duration_ = applied;
    transition_script_ = script;
    NotifyProjectChanged();
    SetStatus(std::string("Added ") + TransitionTypeLabel(type));
}

void VideoEditorAppWidget::ApplyDefaultTransition() {
    // Nearest adjacent pair on a video track to the playhead wins —
    // Ctrl+D in Premiere applies the default transition to the edit
    // under the playhead.
    auto is_video_track = [this](int track) {
        return track >= 0 &&
               track < static_cast<int>(project_.tracks.size()) &&
               project_.tracks[static_cast<std::size_t>(track)].kind ==
                   TrackKind::Video;
    };
    const Clip* from = nullptr;
    const Clip* to = nullptr;
    double best = -1.0;
    for (const Clip& a : project_.clips) {
        if (!is_video_track(a.track)) continue;
        const double cut = ClipEndSeconds(a);
        for (const Clip& b : project_.clips) {
            if (b.track != a.track ||
                std::fabs(b.timeline_start - cut) > 1e-6) {
                continue;
            }
            const double d = std::fabs(cut - playhead_);
            if (best < 0.0 || d < best) {
                best = d;
                from = &a;
                to = &b;
            }
        }
    }
    if (from != nullptr && to != nullptr) {
        AddTransitionPair(from->id, to->id, transition_type_,
                          transition_duration_, transition_script_);
        return;
    }
    // No junction at all — fall back to the selected clip's out-edge
    // (same adjacency resolution as the pickers).
    if (!selected_clip_id_.empty()) {
        AddTransitionAfter(selected_clip_id_, transition_type_, -1.0,
                           transition_script_);
    } else {
        SetStatus("No junction for a transition", true);
    }
}

void VideoEditorAppWidget::ShuttleStep(int direction) {
    constexpr double kMaxShuttleRate = 8.0;
    double rate = shuttle_rate_;
    if (!monitor_->IsPlaying()) {
        rate = static_cast<double>(direction);   // stopped -> ±1x
    } else {
        if (rate == 0.0) rate = 1.0;  // normal play = +1 on the ladder
        if ((rate > 0.0) == (direction > 0)) {
            // Same direction: double, capped at 8x.
            rate = direction > 0
                       ? std::min(rate * 2.0, kMaxShuttleRate)
                       : std::max(rate * 2.0, -kMaxShuttleRate);
        } else {
            // Opposite direction: halve toward zero; at 1x the press
            // reverses onto the other side of the ladder.
            const double mag = std::fabs(rate);
            rate = mag <= 1.0 ? static_cast<double>(direction)
                              : rate * 0.5;
        }
    }
    // Rebuilds land the engine at t=0 — remember so the hot path can
    // re-seek before resuming (a mid-shuttle project edit dirties the
    // snapshot; EnsurePreviewEngine lazily re-inits on next touch).
    const bool rebuilt = preview_dirty_;
    EnsurePreviewEngine();
    if (!preview_ready_) {
        SetStatus("Preview engine unavailable", true);
        return;
    }
    if (!monitor_->IsPlaying()) {
        // Cold start — the same boot sequence as TogglePlay's play
        // branch, then the rate command takes over below.
        if (rate > 0.0 && playhead_ >= project_.duration() - 1e-6) {
            playhead_ = 0.0;
        }
        preview_engine_.Seek(playhead_);
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
        monitor_->SetExternalPlayback(true);
        monitor_->SetPlaying(true);
    } else if (rebuilt) {
        // Re-init mid-shuttle: resume the new session at the current
        // playhead instead of the engine's post-Init t=0.
        preview_engine_.Seek(playhead_);
    }
    // Exclusive transport: a program shuttle starts while the source
    // monitor may still be playing (they share the audio sink).
    StopSourceTransport();
    // SetRate while playing rebases the engine's wall-clock anchor —
    // the playhead never jumps on a rate flip (preview_engine.cc).
    preview_engine_.SetRate(rate);
    preview_engine_.SetPlaying(true);
    monitor_->SetExternalPlayback(true);
    monitor_->SetPlaying(true);
    play_btn_->SetText("Pause");
    shuttle_rate_ = rate;
    char label[32];
    std::snprintf(label, sizeof(label), "x%g", rate);
    SetStatus(std::string("Shuttle ") + label);
}

void VideoEditorAppWidget::ShuttleStop() {
    // Premiere K = pause only (Space toggles) — also the shuttle
    // brake. Mirrors the TogglePlay else-branch; a paused K is a
    // no-op beyond resetting the ladder.
    shuttle_rate_ = 0.0;
    if (!monitor_->IsPlaying()) return;
    preview_engine_.SetRate(1.0);
    preview_engine_.SetPlaying(false);
    monitor_->SetPlaying(false);
    monitor_->SetExternalPlayback(false);
    monitor_->SetTime(playhead_, true);
    play_btn_->SetText("Play");
}

// ============================================================================
// Source monitor — Premiere-style clip preview + three-point edit.
// A second PreviewEngine decodes a synthetic one-clip Project on the
// loaded media file, so pixels are real decode output through the same
// path the program monitor uses. Marks are kept per normalized media
// path (Premiere stores them on the master clip).
// ============================================================================

bool VideoEditorAppWidget::SourceActive() const {
    return source_monitor_ != nullptr && source_monitor_->IsVisible() &&
           !source_path_.empty() &&
           (monitor_target_ == MonitorTarget::Source ||
            source_monitor_->IsFocused());
}

void VideoEditorAppWidget::OnSourceEngineFrame(
    const video::Frame& frame) {
    if (frame.DataSize() == 0 || frame.Width() <= 0 ||
        frame.Height() <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(source_mutex_);
    source_pending_frame_.assign(frame.Data(),
                                 frame.Data() + frame.DataSize());
    source_pending_w_ = frame.Width();
    source_pending_h_ = frame.Height();
    source_has_frame_ = true;
}

void VideoEditorAppWidget::OnSourceEnginePlayhead(double t) {
    std::lock_guard<std::mutex> lock(source_mutex_);
    source_pending_playhead_ = t;
}

void VideoEditorAppWidget::EnsureSourceEngine() {
    if (source_ready_ || source_path_.empty()) return;
    EnsurePlatformAudioSink(&source_audio_sink_);
    if (!source_sink_) {
        source_sink_ = std::make_unique<SourceEngineSink>(this);
    }
    PreviewOptions options;
    options.audio_sink = &source_sink_binding_;
    std::string error;
    source_ready_ =
        source_engine_.Init(source_project_, options,
                            source_sink_.get(), &error);
    if (!source_ready_) {
        SetStatus("Source preview init failed: " + error, true);
        if (source_monitor_) {
            source_monitor_->SetStatusText("Decode failed: " + error,
                                           true);
        }
    }
}

void VideoEditorAppWidget::OpenInSourceMonitor(
    const std::string& path) {
    if (path.empty()) return;
    const CatalogEntry* entry = FindCatalogEntry(path);
    double duration = 0.0, fps = 0.0;
    int w = 0, h = 0;
    bool has_audio = false;
    if (entry != nullptr) {
        duration = entry->duration_seconds;
        w = entry->width;
        h = entry->height;
        fps = entry->fps;
        has_audio = entry->has_audio;
    } else {
        XER::Content::Video::VideoInfo info;
        std::string error;
        if (XER::Content::Video::ProbeVideo(path, &info, &error)) {
            duration = info.duration_seconds;
            w = info.width;
            h = info.height;
            fps = info.fps;
            // ProbeVideo reports stream presence — audio-only
            // containers come back w/h = 0 + has_audio = true; video
            // files with no audio stream report has_audio = false.
            has_audio = info.has_audio;
        } else {
            // Not an FFmpeg container — try the still-image path
            // (EXR/DPX/TIFF/RAW/PSD/HDR via OIIO); the decoder replays
            // a still as a constant frame on the video track.
            const Io::Image still =
                Io::ReadImage(Path::FromUtf8(path));
            if (!still.ok || still.width <= 0 || still.height <= 0) {
                SetStatus("Source open failed: " + error, true);
                return;
            }
            w = still.width;
            h = still.height;
            // duration stays 0 -> kSourceFallbackSeconds below.
        }
    }
    const bool has_video = w > 0 && h > 0;
    if (!has_video && !has_audio) {
        SetStatus("Media has no decodable streams: " + Basename(path),
                  true);
        return;
    }
    // Same still normalization the catalog applies on import
    // (media_catalog.h): a probed one-frame audio-free stream — a PNG
    // /JPEG/... via FFmpeg's image2 demuxer — gets the editor still
    // default instead of its nominal 1-frame duration.
    if (has_video && !has_audio && duration > 0.0 &&
        duration <= kProbedStillMaxSeconds) {
        duration = kStillDefaultSeconds;
    }
    if (duration <= 0.0) duration = kSourceFallbackSeconds;

    StopSourceTransport();
    source_engine_.Shutdown();
    source_ready_ = false;
    source_path_ = Path::NormalizeUtf8Path(path);
    source_name_ = Basename(source_path_);
    source_duration_ = duration;
    source_media_w_ = w;
    source_media_h_ = h;
    source_has_video_ = has_video;
    source_has_audio_ = has_audio;
    source_fps_num_ =
        fps > 0.0 ? std::max(1, static_cast<int>(std::lround(fps)))
                  : (project_.fps_num > 0 ? project_.fps_num : 30);
    source_fps_den_ = 1;

    // Synthetic one-clip project — the engine's compositor path
    // handles scaling/letterboxing to this canvas. Use the media's own
    // dims so decode quality is native.
    source_project_ = Project{};
    source_project_.width = has_video ? w : project_.width;
    source_project_.height = has_video ? h : project_.height;
    source_project_.fps_num = source_fps_num_;
    source_project_.fps_den = source_fps_den_;
    Track track;
    track.name = has_video ? "V1" : "A1";
    track.kind = has_video ? TrackKind::Video : TrackKind::Audio;
    source_project_.tracks.push_back(track);
    Clip clip;
    clip.id = "src";
    clip.source_path = source_path_;
    clip.track = 0;
    clip.duration = duration;
    clip.transform =
        has_video
            ? FitTransformToCanvas(w, h, w, h)
            : Transform{};
    source_project_.clips.push_back(clip);

    // Initial playhead honors a persisted in mark (Premiere drops the
    // source playhead on the mark when a marked clip reopens).
    double initial = 0.0;
    SourceMarks marks;
    if (const auto it = source_marks_.find(source_path_);
        it != source_marks_.end()) {
        marks = it->second;
        if (marks.HasIn()) initial = marks.in;
    }
    source_playhead_ = std::clamp(initial, 0.0, duration);
    source_playing_ = false;
    source_shuttle_rate_ = 0.0;

    if (source_monitor_) {
        source_monitor_->SetSource(source_path_, source_name_, duration,
                                   source_fps_num_, source_fps_den_,
                                   has_video);
        source_monitor_->SetMarks(marks.in, marks.out);
        source_monitor_->SetPlaying(false);
        source_monitor_->SetTime(source_playhead_);
        source_monitor_->SetStatusText(
            has_video ? "Decoding..." : "Audio only");
    }
    source_visible_ = true;
    monitor_target_ = MonitorTarget::Source;

    EnsureSourceEngine();
    if (source_ready_) {
        source_engine_.Seek(source_playhead_);  // paused -> presents
    }
    SetStatus("Source: " + source_name_);
    RequestLayout();
    Invalidate();
}

void VideoEditorAppWidget::SourceSeek(double seconds) {
    if (source_path_.empty() || source_monitor_ == nullptr) return;
    source_playhead_ = std::clamp(seconds, 0.0, source_duration_);
    if (source_ready_) {
        source_engine_.Seek(source_playhead_);
    }
    source_monitor_->SetTime(source_playhead_);
}

void VideoEditorAppWidget::SourceTogglePlay() {
    if (source_path_.empty()) return;
    source_shuttle_rate_ = 0.0;
    const bool next = !source_playing_;
    if (next) {
        // Exclusive transport: program playback yields to the source
        // monitor (the two engines share the audio sink).
        StopProgramTransport();
        EnsureSourceEngine();
        if (!source_ready_) {
            SetStatus("Source preview unavailable", true);
            return;
        }
        if (source_playhead_ >= source_duration_ - 1e-6) {
            source_playhead_ = 0.0;
        }
        source_engine_.Seek(source_playhead_);
        source_engine_.SetRate(1.0);
        source_engine_.SetPlaying(true);
        source_playing_ = true;
    } else {
        source_engine_.SetRate(1.0);
        source_engine_.SetPlaying(false);
        source_playing_ = false;
    }
    if (source_monitor_) source_monitor_->SetPlaying(source_playing_);
}

void VideoEditorAppWidget::StopSourceTransport() {
    source_shuttle_rate_ = 0.0;
    if (!source_playing_ && !source_ready_) return;
    source_engine_.SetRate(1.0);
    source_engine_.SetPlaying(false);
    source_playing_ = false;
    if (source_monitor_) source_monitor_->SetPlaying(false);
}

void VideoEditorAppWidget::SourceShuttleStep(int direction) {
    if (source_path_.empty()) return;
    constexpr double kMaxShuttleRate = 8.0;
    double rate = source_shuttle_rate_;
    if (!source_playing_) {
        rate = static_cast<double>(direction);
    } else {
        if (rate == 0.0) rate = 1.0;
        if ((rate > 0) == (direction > 0)) {
            rate = direction > 0 ? std::min(rate * 2.0, kMaxShuttleRate)
                                 : std::max(rate * 2.0, -kMaxShuttleRate);
        } else {
            rate = std::fabs(rate) <= 1.0 ? direction : rate * 0.5;
        }
    }
    StopProgramTransport();
    EnsureSourceEngine();
    if (!source_ready_) {
        SetStatus("Source preview unavailable", true);
        return;
    }
    if (!source_playing_) {
        if (rate > 0.0 && source_playhead_ >= source_duration_ - 1e-6) {
            source_playhead_ = 0.0;
        }
        source_engine_.Seek(source_playhead_);
    }
    source_engine_.SetRate(rate);
    source_engine_.SetPlaying(true);
    source_playing_ = true;
    source_shuttle_rate_ = rate;
    if (source_monitor_) source_monitor_->SetPlaying(true);
    char label[32];
    std::snprintf(label, sizeof(label), "x%g", rate);
    SetStatus(std::string("Source shuttle ") + label);
}

void VideoEditorAppWidget::SourceShuttleStop() {
    source_shuttle_rate_ = 0.0;
    if (!source_playing_) return;
    source_engine_.SetRate(1.0);
    source_engine_.SetPlaying(false);
    source_playing_ = false;
    if (source_monitor_) source_monitor_->SetPlaying(false);
}

void VideoEditorAppWidget::SourceMark(bool mark_in) {
    if (source_path_.empty() || source_monitor_ == nullptr) return;
    SourceMarks& marks = source_marks_[source_path_];
    if (mark_in) {
        marks.in = source_playhead_;
    } else {
        marks.out = source_playhead_;
    }
    source_monitor_->SetMarks(marks.in, marks.out);
    SetStatus(mark_in ? "Source mark in" : "Source mark out");
}

void VideoEditorAppWidget::SourceClearMark(bool mark_in) {
    if (source_path_.empty() || source_monitor_ == nullptr) return;
    const auto it = source_marks_.find(source_path_);
    if (it == source_marks_.end()) return;
    if (mark_in) {
        it->second.in = -1.0;
    } else {
        it->second.out = -1.0;
    }
    source_monitor_->SetMarks(it->second.in, it->second.out);
    SetStatus(mark_in ? "Source mark in cleared"
                      : "Source mark out cleared");
}

void VideoEditorAppWidget::EditSourceToTimeline(bool insert) {
    if (source_path_.empty()) {
        SetStatus("No clip loaded in the source monitor", true);
        return;
    }
    double src_in = 0.0, src_out = 0.0;
    const auto marks_it = source_marks_.find(source_path_);
    const SourceMarks marks = marks_it != source_marks_.end()
                                  ? marks_it->second
                                  : SourceMarks{};
    if (!ResolveSourceRange(marks, source_duration_, &src_in,
                            &src_out)) {
        SetStatus("Source range is empty — set in/out marks", true);
        return;
    }

    // Target track: the selected clip's track when kind matches, else
    // the first unlocked track of the media kind, else a new one.
    const TrackKind wanted =
        source_has_video_ ? TrackKind::Video : TrackKind::Audio;
    // Premiere-style A/V edit: a source carrying both streams edits as
    // a linked pair — video clip on the video track, audio clip on an
    // audio track, shared link_group (the mixer suppresses the video
    // clip's embedded audio so the stream decodes once).
    const bool linked_av = source_has_video_ && source_has_audio_;
    int track_index = -1;
    if (const Clip* sel = FindClip(project_, selected_clip_id_);
        sel != nullptr && sel->track >= 0 &&
        sel->track < static_cast<int>(project_.tracks.size()) &&
        project_.tracks[sel->track].kind == wanted &&
        !project_.tracks[sel->track].locked) {
        track_index = sel->track;
    }
    if (track_index < 0) {
        for (int i = 0; i < static_cast<int>(project_.tracks.size());
             ++i) {
            const Track& t = project_.tracks[i];
            if (t.kind == wanted && !t.locked) {
                track_index = i;
                break;
            }
        }
    }
    if (track_index < 0) {
        track_index =
            history_.AddTrack(wanted, wanted == TrackKind::Video
                                          ? "Video"
                                          : "Audio");
        if (track_index < 0) {
            SetStatus("Source edit failed: cannot add a track", true);
            return;
        }
    }
    // Audio leg of the linked pair: first unlocked audio track, else
    // a new one (the selected-clip preference only governs the
    // primary/video leg).
    int audio_track = -1;
    if (linked_av) {
        for (int i = 0; i < static_cast<int>(project_.tracks.size());
             ++i) {
            const Track& t = project_.tracks[i];
            if (t.kind == TrackKind::Audio && !t.locked) {
                audio_track = i;
                break;
            }
        }
        if (audio_track < 0) {
            audio_track = history_.AddTrack(TrackKind::Audio, "Audio");
            if (audio_track < 0) {
                SetStatus("Source edit failed: cannot add an audio "
                          "track",
                          true);
                return;
            }
        }
    }

    // Premiere drops an insert at the playhead even when it lands
    // inside a clip — split the straddler first so InsertClipAt's
    // non-overlap validation passes. A linked pair needs the split on
    // BOTH target tracks.
    double at = snap_enabled_
                    ? SnapTime(project_, track_index, playhead_, 0.1,
                               playhead_)
                    : playhead_;
    at = std::max(0.0, at);
    auto straddler_on = [this, at](int track) -> const Clip* {
        for (const Clip& c : project_.clips) {
            if (c.track == track &&
                c.timeline_start < at - 1e-9 &&
                ClipEndSeconds(c) > at + 1e-9) {
                return &c;
            }
        }
        return nullptr;
    };
    const Clip* straddler =
        insert ? straddler_on(track_index) : nullptr;
    const Clip* audio_straddler =
        insert && linked_av ? straddler_on(audio_track) : nullptr;

    Clip clip = MakeSourceClip(source_path_, track_index, at, src_in,
                               src_out, source_media_w_,
                               source_media_h_, project_.width,
                               project_.height);
    clip.id = NewClipId();
    Clip audio_clip;
    if (linked_av) {
        // 0,0 src dims -> identity transform: audio-only leg.
        audio_clip = MakeSourceClip(source_path_, audio_track, at,
                                    src_in, src_out, 0, 0, 0, 0);
        audio_clip.id = NewClipId();
        audio_clip.link_group = NewClipId();
        clip.link_group = audio_clip.link_group;
    }
    const MediaBounds bounds{source_duration_, source_has_video_};
    // Audio-leg bounds: has_video=false marks this clip as audio-only
    // media — accepted on audio tracks, duration-clamped like the
    // primary leg.
    const MediaBounds audio_bounds{source_duration_, false};
    std::string error;

    history_.BeginMacro(insert ? "Insert edit" : "Overwrite edit");
    bool applied_any = false;
    bool ok = true;
    for (const Clip* s : {straddler, audio_straddler}) {
        if (s == nullptr) continue;
        std::string ignored;
        ok = history_.SplitClip(s->id, at, &ignored);
        applied_any |= ok;
        if (!ok) {
            error = "straddling split failed";
            break;
        }
    }
    if (ok) {
        std::string new_id;
        if (insert) {
            ok = history_.InsertClipAt(clip, bounds, &new_id, &error);
        } else {
            ok = history_.OverwriteClipAt(clip, bounds, &new_id,
                                          &error);
        }
        applied_any |= ok;
        if (ok) clip.id = new_id;
    }
    if (ok && linked_av) {
        std::string new_id;
        if (insert) {
            ok = history_.InsertClipAt(audio_clip, audio_bounds,
                                       &new_id, &error);
        } else {
            ok = history_.OverwriteClipAt(audio_clip, audio_bounds,
                                          &new_id, &error);
        }
        applied_any |= ok;
    }
    history_.EndMacro();
    if (!ok) {
        // Undo whatever the macro applied (typically the straddling
        // split) so a failed edit leaves the timeline untouched.
        if (applied_any) history_.Undo();
        SetStatus("Source edit failed: " + error, true);
        return;
    }

    SelectClip(clip.id);
    Seek(clip.timeline_start + clip.duration);
    NotifyProjectChanged();
    SetStatus(std::string(insert ? "Inserted " : "Overwrote ") +
              source_name_);
}

void VideoEditorAppWidget::RippleTrimToPlayhead(bool trim_head) {
    // Clip under the playhead first — the selected clip when it is
    // one, else the lowest-track clip containing the playhead.
    const Clip* target = nullptr;
    if (const Clip* sel = FindClip(project_, selected_clip_id_);
        sel != nullptr && playhead_ > sel->timeline_start &&
        playhead_ < ClipEndSeconds(*sel)) {
        target = sel;
    }
    if (target == nullptr) {
        for (const Clip& c : project_.clips) {
            if (playhead_ > c.timeline_start &&
                playhead_ < ClipEndSeconds(c) &&
                (target == nullptr || c.track < target->track)) {
                target = &c;
            }
        }
    }
    if (target == nullptr) {
        // Playhead on a gap: Q extends the next clip's head back to
        // the playhead, W the previous clip's tail forward — the
        // edit-point trim Premiere applies across a gap.
        for (const Clip& c : project_.clips) {
            if (trim_head) {
                if (c.timeline_start >= playhead_ - 1e-9 &&
                    (target == nullptr ||
                     c.timeline_start < target->timeline_start ||
                     (c.timeline_start == target->timeline_start &&
                      c.track < target->track))) {
                    target = &c;
                }
            } else {
                const double end = ClipEndSeconds(c);
                if (end <= playhead_ + 1e-9 &&
                    (target == nullptr ||
                     end > ClipEndSeconds(*target) ||
                     (end == ClipEndSeconds(*target) &&
                      c.track < target->track))) {
                    target = &c;
                }
            }
        }
    }
    if (target == nullptr) {
        SetStatus("No clip to ripple-trim at the playhead", true);
        return;
    }
    const TrimEdge edge = trim_head ? TrimEdge::In : TrimEdge::Out;
    // Model TrimClip takes a SOURCE-time point — map the playhead
    // through the clip's in-point and playback rate.
    const double source_point =
        target->in_point +
        (playhead_ - target->timeline_start) * target->playback_rate;
    const double edge_source =
        edge == TrimEdge::In
            ? target->in_point
            : target->in_point +
                  target->duration * target->playback_rate;
    if (std::fabs(source_point - edge_source) < 1e-9) {
        SetStatus("Playhead already on that edge");
        return;
    }
    RippleTrimClip(target->id, edge, source_point);
}

void VideoEditorAppWidget::NudgeSelection(int direction) {
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr) {
        SetStatus("Select a clip to nudge", true);
        return;
    }
    const double frame =
        project_.fps_num > 0
            ? static_cast<double>(project_.fps_den) /
                  static_cast<double>(project_.fps_num)
            : 1.0 / 30.0;
    const double delta = static_cast<double>(direction) * frame;
    // Link-group members ride along in the same undo macro — the
    // keyboard counterpart of the drag's per-member hook burst.
    std::vector<const Clip*> movers{clip};
    if (!clip->link_group.empty()) {
        for (const Clip& c : project_.clips) {
            if (c.id != clip->id && c.link_group == clip->link_group) {
                movers.push_back(&c);
            }
        }
    }
    history_.BeginMacro("Nudge");
    bool ok = true;
    std::string error;
    for (const Clip* mover : movers) {
        // Clamp at t=0 — a left nudge at the head parks on the wall
        // instead of failing the whole gesture.
        const double start =
            std::max(0.0, mover->timeline_start + delta);
        if (!history_.MoveClip(mover->id, start,
                               BoundsForPath(mover->source_path),
                               &error)) {
            ok = false;
            break;
        }
    }
    history_.EndMacro();
    if (!ok) {
        SetStatus("Nudge failed: " + error, true);
    } else {
        SetStatus(direction > 0 ? "Nudged +1 frame"
                              : "Nudged -1 frame");
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::RippleDeleteClipById(
    const std::string& clip_id) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to ripple-delete", true);
        return;
    }
    const Clip* doomed = FindClip(project_, clip_id);
    const std::string source =
        doomed != nullptr ? doomed->source_path : std::string{};
    std::string error;
    if (!history_.RippleDeleteClip(clip_id, &error)) {
        SetStatus("Ripple delete failed: " + error, true);
        return;
    }
    if (!source.empty()) media_prep_.CancelQueued(source);
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
    const MediaBounds bounds = BoundsForPath(clip->source_path);
    std::string error;
    history_.BeginMacro("Speed/Duration");
    bool ok = history_.SetClipRate(clip_id, rate, bounds, &error);
    if (!ok && bounds.duration_seconds > 0.0 &&
        clip->in_point + clip->duration * clip->playback_rate >
            bounds.duration_seconds) {
        // The clip ALREADY overflows its media (e.g. the bounds were
        // unknown at insert, or the media was replaced by a shorter
        // file). SetClipRate preserves the consumed span so it can
        // never succeed here — clamp the source window to what fits
        // and apply the rate onto the trimmed clip instead.
        const double avail = bounds.duration_seconds - clip->in_point;
        if (avail > 0.0) {
            Clip fixed = *clip;
            fixed.playback_rate = rate;
            fixed.duration = avail / rate;
            ok = history_.ReplaceClip(clip_id, fixed, bounds, &error);
        }
    }
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
    TitleStyle style =
        TitleStyleFromClipStyle(ParseClipTextStyle(clip->source.param));
    if (style.size_px <= 0) {
        style.size_px = 64;
    }
    title_editor_->SetStyle(style);
    ShowOverlay(title_editor_);
}

void VideoEditorAppWidget::OpenFontManager() {
    if (!font_manager_) {
        return;
    }
    // Pre-select the face of the selected text clip when there is one.
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip != nullptr && clip->source.kind == SourceKind::Text) {
        const ClipTextStyle parsed =
            ParseClipTextStyle(clip->source.param);
        font_manager_->SetSelection(parsed.font_family,
                                    parsed.font_weight, parsed.italic);
    } else {
        font_manager_->SetSelection("", 400, false);
    }
    ShowOverlay(font_manager_);
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
    timeline_height_ = 0.0f;   // drop splitter override; fraction rules
    SetRightTab(ws.right_tab);   // restyles tabs + RequestLayout
    SetStatus("Workspace: " + ws.name);
}

void VideoEditorAppWidget::RefreshWaveformPanel() {
    if (!waveform_panel_) return;
    const Clip* clip = FindClip(project_, selected_clip_id_);
    AudioPeaks peaks;
    // Non-blocking media-prep lookup — a cold decode answers kPending
    // and ConsumeRenderDirty in Tick re-runs this once it lands.
    const bool has =
        clip != nullptr && clip->source.kind == SourceKind::Media &&
        media_prep_.LookupPeaks(clip->source_path, &peaks) ==
            MediaPrep::Lookup::kReady &&
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

void VideoEditorAppWidget::CheckAutosaveRecovery() {
    autosave_offer_ = false;
    autosave_offer_dir_.clear();
    autosave_offer_project_.clear();
    // Only offer when nothing meaningful is loaded — autosaves of an
    // already-open project's edits stay silently available.
    if (!project_.clips.empty()) return;
    if (!project_path_.empty()) {
        // Bound project: slots live in <project dir>/autosave.
        const std::string dir = Path::NormalizeUtf8Path(
            (std::filesystem::path(Path::FromUtf8(project_path_))
                 .parent_path() / "autosave")
                .string());
        const std::vector<std::string> slots =
            AutosaveManager::ListAutosaves(project_path_, dir);
        if (slots.empty()) return;
        autosave_offer_ = true;
        autosave_offer_dir_ = dir;
        autosave_offer_project_ = project_path_;
    } else {
        // Unsaved session: look for an autosave dir next to the CWD.
        // Slot files are "<stem>.autosave-N.xerproj" (N=1 newest) —
        // the newest file's stem identifies the owning project.
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::current_path(ec) / "autosave";
        if (ec || !std::filesystem::is_directory(dir, ec) || ec) return;
        std::string newest_stem;
        std::filesystem::file_time_type newest_time{};
        for (std::filesystem::directory_iterator it(dir, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code tec;
            if (!it->is_regular_file(tec) || tec) continue;
            const std::string name =
                Path::ToUtf8(it->path().filename());
            const std::size_t marker = name.find(".autosave-");
            if (marker == std::string::npos) continue;
            if (name.size() < 8 ||
                name.compare(name.size() - 8, 8, ".xerproj") != 0) {
                continue;
            }
            const auto write_time = it->last_write_time(tec);
            if (tec) continue;
            if (newest_stem.empty() || write_time > newest_time) {
                newest_time = write_time;
                newest_stem = name.substr(0, marker);
            }
        }
        if (newest_stem.empty()) return;
        autosave_offer_ = true;
        autosave_offer_dir_ =
            Path::NormalizeUtf8Path(dir.string());
        autosave_offer_project_ = Path::NormalizeUtf8Path(
            (dir.parent_path() / (newest_stem + ".xerproj")).string());
    }
    SetStatus("Autosave found: " +
              Basename(autosave_offer_project_) +
              " — restore via File ▸ Recover Autosave");
    if (status_toast_) {
        status_toast_->Push(
            "Autosave found — File ▸ Recover Autosave to restore",
            false);
    }
    // Rebuild so the File menu's Recover Autosave item enables.
    RebuildTitleBarMenus();
}

void VideoEditorAppWidget::RecoverAutosave() {
    if (!autosave_offer_) {
        SetStatus("No autosave to recover", true);
        return;
    }
    if (!ConfirmDiscardChanges()) return;
    Project restored;
    std::string error;
    if (!AutosaveManager::LoadLatest(autosave_offer_project_,
                                     autosave_offer_dir_, &restored,
                                     &error)) {
        SetStatus("Autosave restore failed: " + error, true);
        autosave_offer_ = false;
        RebuildTitleBarMenus();
        return;
    }
    history_.Clear();
    project_ = std::move(restored);
    // Keep the originating path only when the primary file still
    // exists — otherwise Save stays a Save-As instead of silently
    // writing next to a deleted project.
    std::error_code ec;
    project_path_ =
        std::filesystem::exists(
            Path::FromUtf8(autosave_offer_project_), ec)
            ? autosave_offer_project_
            : std::string{};
    InitAutosave();
    autosave_offer_ = false;
    selected_clip_id_.clear();
    playhead_ = 0.0;
    NotifyProjectChanged();
    RebuildTitleBarMenus();
    SetStatus(project_path_.empty()
                  ? "Restored autosave (unsaved) — Save As to keep it"
                  : "Restored autosave for " +
                        Basename(autosave_offer_project_));
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

void VideoEditorAppWidget::DetectBeatsForMedia(
    const std::string& path) {
    if (ai_op_.IsRunning() || ai_op_.HasResult()) {
        SetStatus("An AI task is already running", true);
        return;
    }
    SetStatus("Detecting beats: " + Basename(path) + "…");
    ai_progress_shown_ = -1.0f;
    const bool started = ai_op_.Start(
        [this, path](const Lab::JobControl& ctl) {
            Ai::BeatResult beats;
            std::string error;
            // Cap analysis at 5 minutes — plenty for edit-to-the-beat
            // workflows and keeps long media responsive.
            if (!Ai::DetectBeats(path, 300.0, &beats, &error)) {
                ctl.Fail(std::move(error));
                return false;
            }
            {
                std::lock_guard lock(ai_result_mutex_);
                ai_result_.markers.clear();
                for (const double t : beats.times) {
                    ai_result_.markers.emplace_back(t, "Beat");
                }
                if (beats.bpm > 0.0) {
                    ai_result_.detail =
                        std::to_string(
                            static_cast<int>(std::lround(beats.bpm))) +
                        " BPM";
                } else {
                    ai_result_.detail.clear();
                }
            }
            return true;
        });
    if (!started) SetStatus("An AI task is already running", true);
}

void VideoEditorAppWidget::TranscribeMedia(const std::string& path) {
    if (ai_op_.IsRunning() || ai_op_.HasResult()) {
        SetStatus("An AI task is already running", true);
        return;
    }
    std::string detail;
    if (!Ai::WhisperAvailable(&detail)) {
        SetStatus("Whisper unavailable: " + detail, true);
        return;
    }
    const std::string model = Ai::FindWhisperModel();
    if (model.empty()) {
        SetStatus(
            "No whisper model — put ggml-*.bin in models/ or set "
            "XER_WHISPER_MODEL",
            true);
        return;
    }
    SetStatus("Transcribing: " + Basename(path) + "…");
    ai_progress_shown_ = -1.0f;
    const bool started = ai_op_.Start(
        [this, path, model](const Lab::JobControl& ctl) {
            Ai::Pcm pcm;
            std::string error;
            // 16 kHz mono is the whisper contract; cap at 10 minutes.
            if (!Ai::ExtractMonoPcm(path, 16000, 600.0, &pcm, &error)) {
                ctl.Fail(std::move(error));
                return false;
            }
            std::vector<Ai::TranscriptSegment> segments;
            if (!Ai::Transcribe(
                    pcm.samples, model,
                    [&ctl](float p) { ctl.ReportProgress(p); },
                    &segments, &error)) {
                ctl.Fail(std::move(error));
                return false;
            }
            {
                std::lock_guard lock(ai_result_mutex_);
                ai_result_.markers.clear();
                for (const auto& seg : segments) {
                    std::string label = seg.text;
                    if (label.size() > 48) {
                        label = label.substr(0, 45) + "…";
                    }
                    if (label.empty()) label = "Speech";
                    ai_result_.markers.emplace_back(seg.t0,
                                                    std::move(label));
                }
                ai_result_.detail =
                    std::to_string(segments.size()) + " segments";
            }
            return true;
        });
    if (!started) SetStatus("An AI task is already running", true);
}

void VideoEditorAppWidget::BounceAudioFx(
    const std::string& path, const std::string& plugin_id) {
    if (ai_op_.IsRunning() || ai_op_.HasResult()) {
        SetStatus("A task is already running", true);
        return;
    }
    const PluginDesc* desc = SharedPluginHost().Find(plugin_id);
    if (desc == nullptr || desc->audio_fn == nullptr) {
        SetStatus("Audio plugin not found: " + plugin_id, true);
        return;
    }
    const PluginDesc copy = *desc;  // fn pointer stays valid (DLL loaded)
    SetStatus("Audio FX " + copy.name + ": " + Basename(path) + "…");
    ai_progress_shown_ = -1.0f;
    const bool started = ai_op_.Start(
        [this, path, copy](const Lab::JobControl& ctl) {
            Ai::Pcm pcm;
            std::string error;
            // Source rate, mono, capped at 5 minutes — the bounce stays
            // light and the catalog picks it up like any import.
            if (!Ai::ExtractMonoPcm(path, 0, 300.0, &pcm, &error) ||
                pcm.samples.empty() || pcm.sample_rate <= 0) {
                ctl.Fail("decode: " + error);
                return false;
            }
            const int frames = static_cast<int>(pcm.samples.size());
            std::vector<float> params;
            params.reserve(copy.params.size());
            for (const XER_ParamDesc& pd : copy.params) {
                params.push_back(pd.default_value);
            }
            if (!PluginHost::ApplyAudio(copy, pcm.samples.data(), frames,
                                        1, pcm.sample_rate, params)) {
                ctl.Fail("plugin apply failed");
                return false;
            }
            // <stem>.<plugin-id>.wav next to the source.
            const std::filesystem::path src(path);
            const std::string out =
                (src.parent_path() /
                 (src.stem().string() + "." + copy.id + ".wav"))
                    .string();
            if (!WriteWav16(out, pcm.samples.data(), frames, 1,
                            pcm.sample_rate)) {
                ctl.Fail("cannot write " + out);
                return false;
            }
            {
                std::lock_guard lock(ai_result_mutex_);
                ai_result_.markers.clear();
                ai_result_.bounce_path = out;
                ai_result_.detail = copy.name;
            }
            return true;
        });
    if (!started) SetStatus("A task is already running", true);
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

// PageUp/PageDown + Premiere's Shift+M: seek the playhead to the
// previous/next marker. project_.markers stays sorted by time
// (timeline_model.h); the search wraps so the keys always land.
void VideoEditorAppWidget::JumpToMarker(bool next) {
    if (project_.markers.empty()) {
        SetStatus("No markers");
        return;
    }
    constexpr double kEps = 1e-4;
    const Marker* target = nullptr;
    if (next) {
        for (const Marker& marker : project_.markers) {
            if (marker.time > playhead_ + kEps) {
                target = &marker;
                break;
            }
        }
        if (target == nullptr) target = &project_.markers.front();
    } else {
        for (auto it = project_.markers.rbegin();
             it != project_.markers.rend(); ++it) {
            if (it->time < playhead_ - kEps) {
                target = &*it;
                break;
            }
        }
        if (target == nullptr) target = &project_.markers.back();
    }
    Seek(target->time);
    SetStatus("Marker" + (target->name.empty()
                              ? std::string()
                              : ": " + target->name));
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
        if (!history_.SetTrackMuted(track, on, &error)) {
            SetStatus("Mute failed: " + error, true);
            return;
        }
    } else {
        if (!history_.SetTrackHidden(track, on, &error)) {
            SetStatus("Hide failed: " + error, true);
            return;
        }
    }
    NotifyProjectChanged();
}

void VideoEditorAppWidget::SetRightTab(int tab) {
    right_tab_ = std::clamp(tab, 0, 10);   // 0 Insp .. 10 Lyr
    if (right_tabs_) {
        // SetActiveIndex never fires on_changed_ (ActivateTab does),
        // so user clicks and programmatic switches meet here safely.
        right_tabs_->SetActiveIndex(right_tab_);
    }
    if (tab_strip_) {
        // Scrolls the strip so a programmatically-activated tab (View
        // menu, workspace preset, extension UI open) is visible too,
        // not just selected.
        tab_strip_->RevealFraction((right_tab_ + 0.5f) / 11.0f);
    }
    RequestLayout();
    Invalidate();
}

void VideoEditorAppWidget::ApplyProjectSettings(
    const ProjectSettings& settings) {
    // Routed through history_ like every other project mutation so
    // Ctrl+Z restores the previous canvas/fps/background — the command
    // snapshots the five fields (SetProjectSettingsCommand).
    ProjectSettings clamped = settings;
    clamped.w = std::max(16, settings.w);
    clamped.h = std::max(16, settings.h);
    clamped.fps_num = std::max(1, settings.fps_num);
    clamped.fps_den = std::max(1, settings.fps_den);
    if (!history_.Execute(
            std::make_unique<SetProjectSettingsCommand>(clamped))) {
        SetStatus("Project settings failed", true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Project settings applied");
}

int VideoEditorAppWidget::TargetVideoTrack() const {
    // The selected clip's track first — "add a layer above the
    // current one" is the common intent.
    if (const Clip* sel = FindClip(project_, selected_clip_id_);
        sel != nullptr && sel->track >= 0 &&
        sel->track < static_cast<int>(project_.tracks.size()) &&
        project_.tracks[sel->track].kind == TrackKind::Video &&
        !project_.tracks[sel->track].locked) {
        return sel->track;
    }
    for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
        if (project_.tracks[i].kind == TrackKind::Video &&
            !project_.tracks[i].locked) {
            return static_cast<int>(i);
        }
    }
    return -1;   // caller creates a fresh video track
}

void VideoEditorAppWidget::AddGeneratedClip(SourceKind kind) {
    AddGeneratedClipAt(kind, -1, -1.0, "", "", 5.0);
}

void VideoEditorAppWidget::AddGeneratedClipAt(
    SourceKind kind, int track, double at_seconds,
    const std::string& name, const std::string& param,
    double duration) {
    int track_index = track;
    if (track_index < 0) {
        track_index = TargetVideoTrack();
    } else if (track_index >=
                   static_cast<int>(project_.tracks.size()) ||
               project_.tracks[track_index].kind != TrackKind::Video ||
               project_.tracks[track_index].locked) {
        // A context-menu lane that no longer qualifies (kind flipped,
        // locked since the click) falls back like the menu path.
        track_index = TargetVideoTrack();
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
    clip.name = name;
    clip.source.kind = kind;
    clip.track = track_index;
    const double start = at_seconds >= 0.0 ? at_seconds : playhead_;
    clip.timeline_start = snap_enabled_
        ? SnapTime(project_, track_index, start, 0.1, playhead_)
        : start;
    clip.duration = duration > 0.0 ? duration : 5.0;
    if (!param.empty()) {
        clip.source.param = param;
    } else if (kind == SourceKind::ColorMatte) {
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

void VideoEditorAppWidget::ShowNewLayerDialog(int track,
                                            double at_seconds) {
    pending_layer_track_ = track;
    pending_layer_time_ = at_seconds;
    new_layer_dialog_->SetDefaults("", "#3a5fd9", 5.0);
    ShowOverlay(new_layer_dialog_);
}

void VideoEditorAppWidget::ShowAddMediaDialog(int preferred_track,
                                            double at_seconds) {
#ifdef _WIN32
    const auto picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"Media", MediaImportPattern().c_str()),
        L"Add media to timeline");
    if (picked.empty()) return;
    const std::string path =
        Path::NormalizeUtf8Path(Str::WideToUtf8(picked));
    std::string error;
    if (!ImportMediaFile(path, &error)) {
        SetStatus("Import failed: " + error, true);
        return;
    }
    RefreshCatalog();
    const CatalogEntry* imported = FindCatalogEntry(path);
    media_prep_.RequestImportPoster(imported != nullptr ? imported->path
                                                        : path);
    AddMediaToTimelineAt(
        path, at_seconds >= 0.0 ? at_seconds : playhead_,
        preferred_track);
#else
    (void)preferred_track;
    (void)at_seconds;
    SetStatus("Import dialog is Windows-only", true);
#endif
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
#else
    SetStatus("VST3 is Windows-only", true);
#endif
}

void VideoEditorAppWidget::AddScriptAudioFxToSelectedClip(
    const std::string& script_id) {
    const Clip* clip = FindClip(project_, selected_clip_id_);
    if (clip == nullptr) {
        SetStatus("Select a clip first", true);
        return;
    }
    Clip updated = *clip;
    ClipAudioFx fx;
    fx.plugin_path = "script:" + script_id;
    fx.name = script_id;
    updated.audio_fx.push_back(std::move(fx));
    std::string error;
    if (!history_.ReplaceClip(clip->id, updated,
                              BoundsForPath(clip->source_path), &error)) {
        SetStatus("Add JS FX failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added JS audio effect: " + script_id);
}

void VideoEditorAppWidget::OpenVst3FxEditor(const std::string& clip_id,
                                            int fx_index) {
#ifdef _WIN32
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Select a clip first", true);
        return;
    }
    int index = fx_index;
    if (index < 0) {
        // Menu path: the first enabled node is the sensible default.
        for (std::size_t i = 0; i < clip->audio_fx.size(); ++i) {
            if (clip->audio_fx[i].enabled) {
                index = static_cast<int>(i);
                break;
            }
        }
    }
    if (index < 0 ||
        index >= static_cast<int>(clip->audio_fx.size())) {
        SetStatus("Selected clip has no enabled VST3 effect", true);
        return;
    }
    const ClipAudioFx& fx =
        clip->audio_fx[static_cast<std::size_t>(index)];
    std::string error;
    if (!vst3_editor_.Show(fx, parent_hwnd_, &error)) {
        SetStatus("Plugin editor failed: " + error, true);
        return;
    }
    // Bind the session to its fx node — CommitVst3EditorParams writes
    // the edited parameter set back through history on close.
    vst3_editor_clip_ = clip->id;
    vst3_editor_fx_ = index;
    SetStatus("Opened " + fx.name +
              " — parameter edits save on close");
#else
    SetStatus("VST3 is Windows-only", true);
#endif
}

void VideoEditorAppWidget::CommitVst3EditorParams() {
    // Runs inside Vst3PluginEditor::Hide() — the plugin is still
    // alive so CurrentParams() reads the edited values. Bound clip
    // gone (deleted/undone mid-session) -> drop the edits silently.
    if (vst3_editor_clip_.empty()) {
        return;
    }
    const std::string clip_id = vst3_editor_clip_;
    const int fx_index = vst3_editor_fx_;
    vst3_editor_clip_.clear();
    vst3_editor_fx_ = -1;
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr || fx_index < 0 ||
        fx_index >= static_cast<int>(clip->audio_fx.size())) {
        return;
    }
    const auto edits = vst3_editor_.CurrentParams();
    const ClipAudioFx& target =
        clip->audio_fx[static_cast<std::size_t>(fx_index)];
    const std::string name =
        target.name.empty() ? Basename(target.plugin_path)
                            : target.name;
    if (target.params == edits) {
        return;   // nothing changed — avoid a no-op undo step
    }
    Clip updated = *clip;
    updated.audio_fx[static_cast<std::size_t>(fx_index)].params =
        edits;
    std::string error;
    if (!history_.ReplaceClip(clip_id, updated,
                              BoundsForPath(clip->source_path),
                              &error)) {
        SetStatus("Param save failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Saved " + name + " parameters to clip");
}

bool VideoEditorAppWidget::RunLabOperation(
    const LabPanel::RunParams& params, const Lab::JobControl& job) {
    // Worker-thread entry (LabPanel AsyncOp): no UI access here — the
    // result PNG path is stashed and imported from Tick on the UI
    // side. The project/playhead/path snapshot was taken on the UI
    // thread by on_run_prepare; dereferencing live members here would
    // race history_ mutations.
    const auto snapshot =
        std::static_pointer_cast<const LabSnapshot>(params.context);
    if (snapshot == nullptr) {
        job.Fail("Lab run missing project snapshot");
        return false;
    }
    Compositor compositor;
    std::string error;
    if (!compositor.Init(snapshot->project, CompositorOptions{},
                         &error)) {
        job.Fail("Compositor init failed: " + error);
        return false;
    }
    std::vector<std::uint8_t> bgra;
    if (!compositor.RenderFrameAt(snapshot->playhead, &bgra, &error)) {
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
        snapshot->project_path.empty()
            ? std::filesystem::temp_directory_path()
            : std::filesystem::path(
                  Path::FromUtf8(snapshot->project_path)).parent_path();
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
    project_dirty_ = true;   // unsaved-changes flag (save/New/load clears)
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
        mixer_panel_->SetMasterGainDb(project_.master_gain_db);
    }
    if (layers_panel_) {
        layers_panel_->SetProject(&project_);
        layers_panel_->SetActiveTrack(TargetVideoTrack());
        layers_panel_->SetSelectedClip(selected_clip_id_);
    }
    RefreshWaveformPanel();
    monitor_->SetProject(project_);
    if (title_bar_) {
        title_bar_->SetCenterCommandValue(
            project_path_.empty() ? "Untitled.xerproj"
                                  : Basename(project_path_));
    }
    // Selection-dependent Timeline items snapshot selected_clip_id_ at
    // menu build — clip deletes clear it without going through
    // SelectClip(), so refresh here too.
    RebuildTitleBarMenus();
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

std::string VideoEditorAppWidget::DispatchExtensionAction(
    const std::string& action, const std::string& arg_json) {
    if (action == "project_info") {
        char buf[512];
        std::snprintf(
            buf, sizeof buf,
            "{\"width\":%d,\"height\":%d,\"fps_num\":%d,"
            "\"fps_den\":%d,\"tracks\":%zu,\"clips\":%zu,"
            "\"duration\":%.3f,\"master_gain_db\":%.2f}",
            project_.width, project_.height, project_.fps_num,
            project_.fps_den, project_.tracks.size(),
            project_.clips.size(), project_.duration(),
            static_cast<double>(project_.master_gain_db));
        return buf;
    }
    if (action == "status") {
        // Cheap extraction — the host only ever sends {"text": ...}.
        const auto key = arg_json.find("\"text\"");
        std::string text;
        if (key != std::string::npos) {
            const auto colon = arg_json.find(':', key + 6);
            const auto q1 = arg_json.find('"', colon + 1);
            if (q1 != std::string::npos) {
                std::string raw;
                bool esc = false;
                for (size_t i = q1 + 1; i < arg_json.size(); ++i) {
                    const char c = arg_json[i];
                    if (esc) { raw.push_back(c); esc = false; continue; }
                    if (c == '\\') { esc = true; continue; }
                    if (c == '"') break;
                    raw.push_back(c);
                }
                text = raw;
            }
        }
        SetStatus(text);
        return "{\"ok\":true}";
    }
    if (action == "run_extension") {
        const auto key = arg_json.find("\"id\"");
        if (key == std::string::npos) {
            return "{\"error\":\"id required\"}";
        }
        const auto q1 = arg_json.find('"', arg_json.find(':', key));
        const auto q2 = arg_json.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) {
            return "{\"error\":\"id malformed\"}";
        }
        auto r = ExtHost::Shared().Run(arg_json.substr(q1 + 1,
                                                       q2 - q1 - 1));
        if (!r.ok) return "{\"error\":\"" + r.error + "\"}";
        return "{\"ok\":true,\"output\":\"" + r.output + "\"}";
    }
    return "{\"error\":\"unknown extension action: " + action + "\"}";
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
    // Toolbar chrome band under the title bar — the same elevation as
    // the status bar/tab strip so the chrome reads as one frame.
    renderer.FillRect({bounds.x, bounds.y + 38.0f, bounds.width, 44.0f},
                      tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x, bounds.y + 82.0f},
                      {bounds.x + bounds.width, bounds.y + 82.0f},
                      tokens.border, 1.0f);
    // Structural chrome cached by Layout(): the status bar gets its
    // own surface and the timeline split gets a hairline divider. The
    // right-panel tab strip paints itself now (Widgets::Tabs draws
    // the bar, hairline and animated underline indicator).
    if (status_bar_top_ > 0.0f) {
        renderer.FillRect({bounds.x, status_bar_top_, bounds.width,
                           bounds.height - status_bar_top_},
                          tokens.panel, 0.0f);
        renderer.DrawLine({bounds.x, status_bar_top_},
                          {bounds.x + bounds.width, status_bar_top_},
                          tokens.border, 1.0f);
    }
    if (timeline_top_ > 0.0f) {
        renderer.DrawLine({bounds.x, timeline_top_},
                          {bounds.x + bounds.width, timeline_top_},
                          tokens.border, 1.0f);
    }
    // Splitter affordances: a thin centered line inside each gap strip —
    // border-subtle at rest, accent and slightly thicker on hover/drag.
    const auto draw_splitter =
        [&](const XER::UI::Layout::Rect& rect, int index,
            bool vertical) {
            if (rect.width <= 0.0f || rect.height <= 0.0f) return;
            const bool active =
                drag_splitter_ == index || hover_splitter_ == index;
            const auto& color = active ? tokens.accent : tokens.border;
            const float thick = active ? 2.0f : 1.0f;
            if (vertical) {
                renderer.FillRect(
                    {rect.x + rect.width * 0.5f - thick * 0.5f, rect.y,
                     thick, rect.height},
                    color, 0.0f);
            } else {
                renderer.FillRect(
                    {rect.x,
                     rect.y + rect.height * 0.5f - thick * 0.5f,
                     rect.width, thick},
                    color, 0.0f);
            }
        };
    draw_splitter(left_splitter_rect_, 0, true);
    draw_splitter(right_splitter_rect_, 1, true);
    draw_splitter(timeline_splitter_rect_, 2, false);
}

void VideoEditorAppWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    InstallGlobalShortcuts();
    // Tree-level context-menu dispatch: any right-click a widget didn't
    // consume lands in OnContextMenu() and is routed by region. Same
    // lazy-register pattern as InstallGlobalShortcuts — retries until
    // the tree is attached.
    if (!context_menu_registered_) {
        if (XER::UI::Widgets::WidgetTree* tree = GetTree();
            tree != nullptr) {
            context_menu_registered_ = true;
            tree->SetContextMenuHandler(
                [this](const Widgets::Event& event) {
                    return OnContextMenu(event);
                });
        }
    }
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float gap = tokens.spacing_small;

    title_bar_->Layout({bounds.x, bounds.y, bounds.width, 38.0f}, tokens);

    const float toolbar_y = bounds.y + 38.0f;
    const float toolbar_h = 44.0f;
    // Outer margin uses the 12px content-padding step of the grid.
    const float margin = tokens.spacing_medium;
    // Export stays pinned right; the left button row hides buttons that
    // would collide with it instead of drawing underneath.
    const float export_x = bounds.x + bounds.width - margin - 84.0f;
    float x = bounds.x + margin;
    const auto place = [&](const std::shared_ptr<Widgets::Button>& button,
                           float width) {
        const bool fits = x + width <= export_x - gap;
        button->SetVisible(fits);
        if (fits) {
            button->Layout({x, toolbar_y + 7.0f, width, 30.0f}, tokens);
        }
        x += width + gap;
    };
    // Row order comes from toolbar_row_ (built in EnsureChildren):
    // toolbar.json order when the assets layer loaded, else compiled.
    for (const auto& [button, width] : toolbar_row_) {
        place(button, width);
    }
    const float zoom_x = x + 4.0f;
    const float zoom_w = export_x - gap - zoom_x;
    zoom_label_->SetVisible(zoom_w >= 48.0f);
    if (zoom_w > 0.0f) {
        zoom_label_->Layout({zoom_x, toolbar_y + 10.0f, zoom_w, 22.0f},
                            tokens);
    }
    export_btn_->Layout({export_x, toolbar_y + 7.0f, 84.0f, 30.0f},
                        tokens);

    const float status_h = 26.0f;
    const float content_top = toolbar_y + toolbar_h;
    const float content_bottom = bounds.y + bounds.height - status_h;
    const float content_h =
        std::max(0.0f, content_bottom - content_top);
    // Optional selected-clip waveform strip sits between the central
    // row and the timeline.
    const float wave_h =
        (waveform_panel_ && waveform_panel_->IsVisible()) ? 84.0f : 0.0f;
    // The timeline yields height before the central row is crushed:
    // never push the monitor/panels below a usable strip or past the
    // status bar on short windows.
    const float max_timeline_h =
        std::max(110.0f, content_h - 100.0f - gap * 2.0f - wave_h);
    // A splitter drag pins an exact pixel height; otherwise the
    // workspace fraction drives the timeline band.
    const float timeline_h = std::min(
        timeline_height_ > 0.0f
            ? timeline_height_
            : std::clamp(content_h *
                             static_cast<float>(timeline_fraction_),
                         170.0f, 340.0f),
        max_timeline_h);
    const float central_h = std::max(
        60.0f, content_h - timeline_h - gap * 2.0f - wave_h);

    // Icon rail on the far left, below the toolbar and above the
    // status bar — everything else shifts right by its width.
    constexpr float kRailW = 46.0f;
    // Side panels keep their preferred sizes but shrink — then collapse —
    // so the monitor never goes under 220px or overlaps them.
    constexpr float kMinMonitorW = 220.0f;
    // Panel widths come from the drag overrides (seeded from
    // assets/ui/layout.yaml Panels.left/right.width in EnsureChildren);
    // a 0 override falls back to the proportional default.
    float left_w = left_panel_visible_
        ? (left_width_ > 0.0f
               ? left_width_
               : std::clamp(bounds.width * 0.20f, 230.0f, 320.0f))
        : 0.0f;
    float right_w = right_panel_visible_
        ? (right_width_ > 0.0f
               ? right_width_
               : std::clamp(bounds.width * 0.22f, 250.0f, 340.0f))
        : 0.0f;
    const auto monitor_avail = [&] {
        return bounds.width - kRailW - margin * 2.0f - left_w - right_w;
    };
    if (monitor_avail() < kMinMonitorW) {
        right_w =
            std::max(0.0f, right_w - (kMinMonitorW - monitor_avail()));
        if (right_w < 200.0f) right_w = 0.0f;
    }
    if (monitor_avail() < kMinMonitorW) {
        left_w =
            std::max(0.0f, left_w - (kMinMonitorW - monitor_avail()));
        if (left_w < 160.0f) left_w = 0.0f;
    }
    const bool left_eff = left_panel_visible_ && left_w > 0.0f;
    const bool right_eff = right_panel_visible_ && right_w > 0.0f;

    const float rail_x = bounds.x;
    nav_rail_->Layout({rail_x, content_top, kRailW,
                       content_bottom - content_top},
                      tokens);
    const float content_x = rail_x + kRailW;

    float panel_x = content_x + margin;
    const float panel_y = content_top + gap;
    media_bin_->SetVisible(left_eff);
    media_bin_->SetPreviewActive(left_eff);
    if (left_eff) {
        media_bin_->Layout({panel_x, panel_y, left_w - gap,
                            std::max(80.0f, central_h)}, tokens);
        panel_x += left_w;
    }
    const float inspector_x =
        bounds.x + bounds.width - margin - right_w + gap;
    const float monitor_w = std::max(
        60.0f, bounds.x + bounds.width - margin - panel_x - right_w);
    // Premiere layout: Source Monitor left, Program Monitor right. The
    // source slot only appears once a clip is loaded or the user
    // toggled it on (View > Source Monitor); below ~2x220px it
    // collapses back to a single program surface.
    const bool show_source =
        source_monitor_ != nullptr && source_visible_ &&
        monitor_w >= 440.0f;
    if (source_monitor_) source_monitor_->SetVisible(show_source);
    if (show_source) {
        const float source_w = (monitor_w - gap) * 0.5f;
        source_monitor_->Layout(
            {panel_x, panel_y, source_w, std::max(80.0f, central_h)},
            tokens);
        monitor_->Layout({panel_x + source_w + gap, panel_y,
                          monitor_w - source_w - gap,
                          std::max(80.0f, central_h)},
                         tokens);
    } else {
        monitor_->Layout({panel_x, panel_y, monitor_w,
                          std::max(80.0f, central_h)}, tokens);
    }
    // Right column: tab strip over the active panel — Premiere's
    // stacked panel group (Inspector/Effects/Markers/Lab/History/Mix).
    inspector_->SetVisible(right_eff && right_tab_ == 0);
    effects_panel_->SetVisible(right_eff && right_tab_ == 1);
    markers_panel_->SetVisible(right_eff && right_tab_ == 2);
    lab_panel_->SetVisible(right_eff && right_tab_ == 3);
    history_panel_->SetVisible(right_eff && right_tab_ == 4);
    mixer_panel_->SetVisible(right_eff && right_tab_ == 5);
    export_queue_->SetVisible(right_eff && right_tab_ == 6);
    plugins_panel_->SetVisible(right_eff && right_tab_ == 7);
    color_panel_->SetVisible(right_eff && right_tab_ == 8);
    browser_panel_->SetVisible(right_eff && right_tab_ == 9);
    if (layers_panel_) {
        layers_panel_->SetVisible(right_eff && right_tab_ == 10);
    }
    tab_strip_->SetVisible(right_eff);
    if (right_eff) {
        const float tab_h = 30.0f;
        // The scroll host clips/lays out the Tabs child at its
        // natural width — see TabStripScroll.
        tab_strip_->Layout({inspector_x, panel_y, right_w - gap,
                            tab_h},
                           tokens);
        std::shared_ptr<Widgets::Widget> panels[] = {
            inspector_,  effects_panel_, markers_panel_, lab_panel_,
            history_panel_, mixer_panel_, export_queue_,
            plugins_panel_, color_panel_, browser_panel_,
            layers_panel_};
        panels[std::clamp(right_tab_, 0, 10)]->Layout(
            {inspector_x, panel_y + tab_h, right_w - gap,
             std::max(60.0f, central_h - tab_h)}, tokens);
    }
    if (wave_h > 0.0f) {
        waveform_panel_->Layout(
            {content_x + 10.0f, panel_y + central_h + gap,
             bounds.width - kRailW - 20.0f, wave_h - gap}, tokens);
    }
    const float timeline_y = panel_y + central_h + gap + wave_h;
    timeline_->Layout(
        {content_x + 10.0f, timeline_y, bounds.width - kRailW - 20.0f,
         timeline_h}, tokens);
    timeline_top_ = timeline_y;
    // Splitter hit-rects: 6px bars centered on the gap strips the child
    // layout leaves between panels (gap = 8px, so a 6px rect fits fully
    // inside). Events in those strips hit this widget — not a child —
    // which is what makes HandleEvent() drags work.
    constexpr float kSplitterW = 6.0f;
    left_splitter_rect_ = {};
    right_splitter_rect_ = {};
    timeline_splitter_rect_ = {};
    if (left_eff) {
        // Left gap strip spans [panel_x - gap, panel_x).
        left_splitter_rect_ = {
            panel_x - gap * 0.5f - kSplitterW * 0.5f, panel_y,
            kSplitterW, std::max(80.0f, central_h)};
    }
    if (right_eff) {
        // Right gap strip spans [inspector_x - gap, inspector_x).
        right_splitter_rect_ = {
            inspector_x - gap * 0.5f - kSplitterW * 0.5f, panel_y,
            kSplitterW, std::max(80.0f, central_h)};
    }
    // Timeline gap strip spans [timeline_y - gap, timeline_y).
    timeline_splitter_rect_ = {
        content_x + 10.0f, timeline_y - gap * 0.5f - kSplitterW * 0.5f,
        bounds.width - kRailW - 20.0f, kSplitterW};
    // Resolved sizes for the next drag's start deltas.
    last_left_w_ = left_w;
    last_right_w_ = right_w;
    last_timeline_h_ = timeline_h;
    status_bar_top_ = content_bottom;
    if (status_toast_) {
        // Click-through overlay spanning the whole window.
        status_toast_->Layout(bounds, tokens);
    }
    // The strip's right edge always reserves the sync-cancel slot —
    // hiding the button shifts nothing else around.
    status_label_->Layout({bounds.x + 12.0f, content_bottom + 4.0f,
                           bounds.width - 112.0f, 20.0f}, tokens);
    if (sync_cancel_btn_) {
        sync_cancel_btn_->Layout(
            {bounds.x + bounds.width - 88.0f, content_bottom + 2.0f,
             80.0f, 22.0f},
            tokens);
    }
}

bool VideoEditorAppWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using Widgets::Event;
    using Widgets::KeyCode;
    // Panel splitters: hit-rects are cached by Layout() inside the gap
    // strips between children, so presses land on this widget and the
    // tree's left-button capture routes the whole drag here.
    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == Widgets::MouseButton::Left) {
        int hit = -1;
        float start_size = 0.0f;
        if (left_splitter_rect_.width > 0.0f &&
            left_splitter_rect_.Contains(event.position)) {
            hit = 0;
            start_size = last_left_w_;
        } else if (right_splitter_rect_.width > 0.0f &&
                   right_splitter_rect_.Contains(event.position)) {
            hit = 1;
            start_size = last_right_w_;
        } else if (timeline_splitter_rect_.width > 0.0f &&
                   timeline_splitter_rect_.Contains(event.position)) {
            hit = 2;
            start_size = last_timeline_h_;
        }
        if (hit >= 0) {
            drag_splitter_ = hit;
            hover_splitter_ = hit;
            drag_start_ =
                hit == 2 ? event.position.y : event.position.x;
            drag_start_size_ = start_size;
            Invalidate();
            return true;
        }
    }
    if (event.type == Event::Type::MouseMove) {
        if (drag_splitter_ >= 0) {
            const float delta = drag_splitter_ == 2
                ? event.position.y - drag_start_
                : event.position.x - drag_start_;
            switch (drag_splitter_) {
                case 0: {  // left panel's right edge follows the cursor
                    const float w = drag_start_size_ + delta;
                    // Drag-through collapse like Premiere: under ~120px
                    // the panel snaps hidden (View menu/toggles restore).
                    if (w < 120.0f) {
                        left_panel_visible_ = false;
                        drag_splitter_ = -1;
                    } else {
                        left_width_ = std::clamp(w, 160.0f, 600.0f);
                    }
                    break;
                }
                case 1: {  // right panel: width grows leftward
                    const float w = drag_start_size_ - delta;
                    if (w < 120.0f) {
                        right_panel_visible_ = false;
                        drag_splitter_ = -1;
                    } else {
                        right_width_ = std::clamp(w, 160.0f, 600.0f);
                    }
                    break;
                }
                case 2:  // timeline top edge: height grows upward
                    timeline_height_ = std::clamp(
                        drag_start_size_ - delta, 120.0f, 500.0f);
                    break;
                default:
                    break;
            }
            RequestLayout();
            return true;
        }
        // Not dragging: track splitter hover for the accent tint.
        int hover = -1;
        if (left_splitter_rect_.width > 0.0f &&
            left_splitter_rect_.Contains(event.position)) {
            hover = 0;
        } else if (right_splitter_rect_.width > 0.0f &&
                   right_splitter_rect_.Contains(event.position)) {
            hover = 1;
        } else if (timeline_splitter_rect_.width > 0.0f &&
                   timeline_splitter_rect_.Contains(event.position)) {
            hover = 2;
        }
        if (hover != hover_splitter_) {
            hover_splitter_ = hover;
            Invalidate();
        }
    }
    if (event.type == Event::Type::MouseUp && drag_splitter_ >= 0) {
        drag_splitter_ = -1;
        Invalidate();
        return true;
    }
    if (event.type == Event::Type::TextInput && event.text.size() == 1 &&
        !event.is_ctrl && event.is_alt) {
        // Alt+I / Alt+O clear the source monitor's marks (Premiere
        // "Clear In / Clear Out"). Only when a source is loaded —
        // otherwise let the event bubble.
        const char alt_key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(event.text.front())));
        if (alt_key == 'i' && SourceActive()) {
            SourceClearMark(true);
            return true;
        }
        if (alt_key == 'o' && SourceActive()) {
            SourceClearMark(false);
            return true;
        }
        return false;
    }
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
                // Premiere: Shift+M jumps to the next marker; a bare M
                // drops one. Caps Lock sends 'M' with is_shift=false —
                // still adds.
                if (event.is_shift) {
                    JumpToMarker(true);
                } else {
                    AddMarkerAt(playhead_, "");
                }
                return true;
            case 'i': case 'I':
                if (SourceActive()) {
                    SourceMark(true);
                } else {
                    SetWorkAreaRange(playhead_, project_.work_out);
                }
                return true;
            case 'o': case 'O':
                if (SourceActive()) {
                    SourceMark(false);
                } else {
                    SetWorkAreaRange(project_.work_in, playhead_);
                }
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
                if (SourceActive()) {
                    SourceShuttleStep(-1);
                } else {
                    ShuttleStep(-1);
                }
                return true;
            case 'k': case 'K':
                if (SourceActive()) {
                    SourceShuttleStop();
                } else {
                    ShuttleStop();
                }
                return true;
            case 'l': case 'L':
                if (SourceActive()) {
                    SourceShuttleStep(1);
                } else {
                    ShuttleStep(1);
                }
                return true;
            case 'q': case 'Q':
                // Premiere Q: ripple-trim the head to the playhead.
                RippleTrimToPlayhead(true);
                return true;
            case 'w': case 'W':
                // Premiere W: ripple-trim the tail to the playhead.
                RippleTrimToPlayhead(false);
                return true;
            case ',':
                // Premiere: , = insert edit / . = overwrite edit from
                // the source monitor; with the program monitor active
                // they nudge the selected clip.
                if (SourceActive()) {
                    EditSourceToTimeline(true);
                } else {
                    NudgeSelection(-1);
                }
                return true;
            case '.':
                if (SourceActive()) {
                    EditSourceToTimeline(false);
                } else {
                    NudgeSelection(1);
                }
                return true;
            case '?':
                ToggleShortcutsOverlay();
                return true;
            default:
                return false;
        }
    }
    if (event.type != Event::Type::KeyDown) return false;
    if (event.key == KeyCode::Escape && !event.is_ctrl &&
        !event.is_alt && sync_op_.IsRunning()) {
        // Esc = the status-strip Cancel button — same kill path
        // (cancel flag + disposable-worker kill), one keystroke less.
        RequestSyncCancel();
        return true;
    }
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
    if (event.is_ctrl && event.key == KeyCode::D) {
        ApplyDefaultTransition();
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Space) {
        if (SourceActive()) {
            SourceTogglePlay();
        } else {
            TogglePlay();
        }
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::ArrowLeft) {
        const double step =
            event.is_shift ? 5.0
                           : 1.0 / (SourceActive() &&
                                            source_fps_num_ > 0
                                        ? static_cast<double>(
                                              source_fps_num_) /
                                              source_fps_den_
                                        : 30.0);
        if (SourceActive()) {
            SourceSeek(source_playhead_ - step);
        } else {
            Seek(playhead_ - step);
        }
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::ArrowRight) {
        const double step =
            event.is_shift ? 5.0
                           : 1.0 / (SourceActive() &&
                                            source_fps_num_ > 0
                                        ? static_cast<double>(
                                              source_fps_num_) /
                                              source_fps_den_
                                        : 30.0);
        if (SourceActive()) {
            SourceSeek(source_playhead_ + step);
        } else {
            Seek(playhead_ + step);
        }
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Home) {
        if (SourceActive()) {
            SourceSeek(0.0);
        } else {
            Seek(0.0);
        }
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::End) {
        if (SourceActive()) {
            SourceSeek(source_duration_);
        } else {
            Seek(project_.duration());
        }
        return true;
    }
    // Marker nav — PageUp/PageDown jump the playhead to the
    // previous/next timeline marker (Shift+M also goes forward).
    if (!event.is_ctrl && event.key == KeyCode::PageDown) {
        JumpToMarker(true);
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::PageUp) {
        JumpToMarker(false);
        return true;
    }
    if (!event.is_ctrl && event.key == KeyCode::Delete) {
        // Alt+Del = ripple delete even when the timeline widget itself
        // doesn't have focus (timeline's on_ripple_delete path).
        if (event.is_alt) {
            RippleDeleteClipById(selected_clip_id_);
        } else {
            DeleteClip(selected_clip_id_);
        }
        return true;
    }
    return false;
}

void VideoEditorAppWidget::Tick(double dt_seconds) {
    if (status_toast_) status_toast_->Tick(dt_seconds);
    // Audio-sink retry lives here — EnsurePreviewEngine early-returns
    // once initialized, so a device that failed to open at startup
    // (driver busy, endpoint renegotiating) would never recover. The
    // late-binding sink forwards to the new device without re-Init.
    if (preview_ready_ && !program_audio_sink_) {
        EnsurePlatformAudioSink(&program_audio_sink_);
    }
    if (source_ready_ && !source_audio_sink_) {
        EnsurePlatformAudioSink(&source_audio_sink_);
    }
    // Media-prep completions: fresh posters/peaks need a repaint;
    // import-poster jobs wrote poster_path rows -> refresh the bin.
    // NotifyMediaChanged drops the timeline's cached media-duration
    // misses so waveforms realign once the probe data exists.
    if (media_prep_.ConsumeRenderDirty()) {
        if (timeline_) {
            timeline_->NotifyMediaChanged();
            timeline_->Invalidate();
        }
        // Bin tiles draw peaks too — let their cached misses re-probe
        // and repaint once waveforms land.
        if (media_bin_) {
            media_bin_->NotifyMediaChanged();
            media_bin_->Invalidate();
        }
        RefreshWaveformPanel();
    }
    if (media_prep_.ConsumeCatalogDirty()) {
        RefreshCatalog();
        if (timeline_) timeline_->NotifyMediaChanged();
    }
    // Import-poster requests dropped on a full queue are never retried
    // — surface them coalesced, rate-limited, instead of spamming the
    // status bar per drop.
    if (const std::uint64_t dropped =
            media_prep_.ConsumeDroppedPosterRequests();
        dropped > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now - poster_drop_warned_at_ > std::chrono::seconds(5)) {
            poster_drop_warned_at_ = now;
            SetStatus("Media queue busy — skipped " +
                          std::to_string(dropped) +
                          " poster decode(s)",
                      true);
        }
    }
    // Shared AI job: land finished results (markers) on the UI thread.
    {
        bool ok = false, cancelled = false;
        std::string ai_error;
        if (ai_op_.PopResult(&ok, &cancelled, &ai_error)) {
            ai_progress_shown_ = -1.0f;
            AiResult result;
            {
                std::lock_guard lock(ai_result_mutex_);
                result = std::move(ai_result_);
                ai_result_ = AiResult{};
            }
            if (ok && !result.bounce_path.empty()) {
                std::int64_t id = 0;
                std::string imp_err;
                // Poster decode goes to the media-prep worker — same
                // fast-import pattern as ImportMedia().
                if (catalog_.Open(&imp_err) &&
                    catalog_.Import(result.bounce_path, &id, false,
                                    &imp_err)) {
                    SetStatus("Audio FX bounce imported: " +
                              Basename(result.bounce_path));
                    RefreshCatalog();
                    media_prep_.RequestImportPoster(
                        result.bounce_path);
                } else {
                    SetStatus("Bounce import failed: " + imp_err, true);
                }
            } else if (ok && !result.markers.empty()) {
                history_.BeginMacro("AI markers");
                int added = 0;
                for (const auto& [t, name] : result.markers) {
                    Marker marker;
                    marker.time = t;
                    marker.name = name;
                    if (history_.AddMarker(marker, nullptr)) ++added;
                }
                history_.EndMacro();
                NotifyProjectChanged();
                SetStatus("AI: added " + std::to_string(added) +
                          " marker(s)" +
                          (result.detail.empty()
                               ? ""
                               : " — " + result.detail));
            } else if (ok) {
                SetStatus("AI finished — nothing to add");
            } else {
                SetStatus(cancelled ? "AI task cancelled"
                                    : "AI failed: " + ai_error,
                          true);
            }
        } else if (ai_op_.IsRunning()) {
            const float p = ai_op_.Progress();
            if (p != ai_progress_shown_ && p > 0.0f) {
                ai_progress_shown_ = p;
                SetStatus("AI running: " +
                          std::to_string(static_cast<int>(p * 100.0f)) +
                          "%");
            }
        }
    }
    // URL-import job: the worker only downloads; the produced file is
    // imported + postered here on the UI thread — the same fast-import
    // pattern as ImportMedia() (extract_poster=false, media-prep picks
    // up the poster via ConsumeCatalogDirty).
    {
        bool ok = false, cancelled = false;
        std::string url_error;
        if (url_op_.PopResult(&ok, &cancelled, &url_error)) {
            url_progress_shown_ = -1.0f;
            UrlImport::Result result;
            {
                std::lock_guard lock(url_result_mutex_);
                result = std::move(url_result_);
                url_result_ = UrlImport::Result{};
                url_status_.clear();
            }
            if (ok && !result.path.empty()) {
                std::string import_error;
                if (ImportMediaFile(result.path, &import_error)) {
                    RefreshCatalog();
                    const CatalogEntry* imported =
                        FindCatalogEntry(result.path);
                    media_prep_.RequestImportPoster(
                        imported != nullptr ? imported->path
                                            : result.path);
                    SetStatus("Imported " + Basename(result.path) +
                              (result.used_ytdlp ? " (yt-dlp)" : ""));
                } else {
                    SetStatus("URL import failed: " + import_error,
                              true);
                }
            } else {
                SetStatus(cancelled ? "URL import cancelled"
                                    : "URL import failed: " + url_error,
                          !cancelled);
            }
        } else if (url_op_.IsRunning()) {
            // Stage text comes from the worker (resolving/downloading/
            // yt-dlp %); fold the fraction in when it reports one.
            std::string stage;
            {
                std::lock_guard lock(url_result_mutex_);
                stage = url_status_;
            }
            const float p = url_op_.Progress();
            if (p > 0.0f) {
                const int pct =
                    static_cast<int>(p * 100.0f + 0.5f);
                if (p != url_progress_shown_) {
                    url_progress_shown_ = p;
                    SetStatus("Downloading… " +
                              std::to_string(pct) + "%");
                }
            } else if (!stage.empty() &&
                       url_progress_shown_ != -2.0f) {
                url_progress_shown_ = -2.0f;
                SetStatus(stage);
            }
        }
    }
    // Audio-sync job: progress while it runs; the finished plan lands
    // as history-wrapped clip moves on this thread (the worker only
    // analysed a snapshot). The Cancel button mirrors the job's
    // lifetime — hidden when idle.
    {
        bool ok = false, cancelled = false;
        std::string sync_error;
        if (sync_op_.PopResult(&ok, &cancelled, &sync_error)) {
            sync_progress_shown_ = -1;
            AudioSyncReport report;
            {
                std::lock_guard lock(sync_result_mutex_);
                report = std::move(sync_report_);
                sync_report_ = AudioSyncReport{};
            }
            // A cancel requested while the plan was mid-flight wins
            // over a late kPlanned result — the plan never mutated
            // the project, so dropping the report is safe.
            if (cancelled ||
                sync_cancel_.load(std::memory_order_relaxed)) {
                SetStatus("Sync cancelled");
            } else if (ok) {
                int applied = 0;
                // Every accepted move folds into ONE undo step
                // ("Audio sync"). Clips are re-resolved against the
                // LIVE project — edits made while the plan ran leave
                // stale entries to be skipped, never moved blind.
                history_.BeginMacro("Audio sync");
                for (const auto& e : report.entries) {
                    if (!e.matched) continue;
                    const Clip* clip = FindClip(project_, e.clip_id);
                    if (clip == nullptr) continue;
                    std::string move_err;
                    if (history_.MoveClip(
                            e.clip_id, e.new_timeline_start,
                            BoundsForPath(clip->source_path),
                            &move_err)) {
                        ++applied;
                    }
                }
                history_.EndMacro();
                SetStatus(report.status + " · " +
                          std::to_string(applied) + " moved");
                if (applied > 0) NotifyProjectChanged();
            } else {
                SetStatus("Audio sync: " + sync_error, true);
            }
        } else if (sync_op_.IsRunning()) {
            const int done =
                sync_progress_done_.load(std::memory_order_relaxed);
            const int total =
                sync_progress_total_.load(std::memory_order_relaxed);
            if (total > 0 && done != sync_progress_shown_) {
                sync_progress_shown_ = done;
                SetStatus("Syncing audio… " + std::to_string(done) +
                          "/" + std::to_string(total) + " clips");
            }
            // total == 0: the remote path reports no progress — the
            // "Syncing audio…" text set at Start() stands.
        }
        const bool sync_running = sync_op_.IsRunning();
        if (sync_running &&
            sync_cancel_.load(std::memory_order_relaxed)) {
            // Repeat the kill while the cancel is pending — the same
            // 20 ms cadence the old in-line poll had, now on the tick:
            // a KillWorker() that raced ahead of the lazy spawn would
            // no-op and leave the remote plan running to completion.
            SyncMediaPipe().KillWorker();
        }
        if (sync_cancel_btn_ != nullptr &&
            sync_cancel_btn_->IsVisible() != sync_running) {
            sync_cancel_btn_->SetVisible(sync_running);
            Invalidate();
        }
    }
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
    // Marshal PreviewEngine worker callbacks onto widgets — copy the
    // pending values out under the mutex, then apply them unlocked:
    // SetExternalFrame is a full-frame memcpy + display transform +
    // scope recompute, and holding the lock through it stalls
    // OnEngineFrame for the whole widget update (frame-drop jitter).
    std::vector<std::uint8_t> tick_frame;
    int tick_frame_w = 0, tick_frame_h = 0;
    bool has_frame = false, has_peaks = false;
    float tick_peak_l = 0.0f, tick_peak_r = 0.0f;
    double tick_playhead = -1.0;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        if (has_pending_frame_) {
            tick_frame.swap(pending_frame_);  // take the buffer, no copy
            tick_frame_w = pending_frame_w_;
            tick_frame_h = pending_frame_h_;
            has_pending_frame_ = false;
            has_frame = true;
        }
        if (has_pending_peaks_) {
            tick_peak_l = pending_peak_l_;
            tick_peak_r = pending_peak_r_;
            has_pending_peaks_ = false;
            has_peaks = true;
        }
        if (pending_playhead_ >= 0.0) {
            tick_playhead = pending_playhead_;
            pending_playhead_ = -1.0;
        }
    }
    if (has_frame) {
        monitor_->SetExternalFrame(tick_frame, tick_frame_w,
                                   tick_frame_h);
    }
    if (has_peaks) {
        monitor_->SetPeakLevels(tick_peak_l, tick_peak_r);
        if (mixer_panel_) {
            mixer_panel_->SetPeak(-1, tick_peak_l, tick_peak_r);
        }
    }
    if (tick_playhead >= 0.0) {
        playhead_ = std::clamp(tick_playhead, 0.0, project_.duration());
        timeline_->SetPlayhead(playhead_);
        inspector_->SetPlayhead(playhead_);
        if (markers_panel_) markers_panel_->SetPlayhead(playhead_);
        if (waveform_panel_ && waveform_panel_->IsVisible()) {
            waveform_panel_->SetPlayhead(playhead_);
        }
        monitor_->SetTime(playhead_, false);
        if (preview_ready_ && !preview_engine_.Playing() &&
            monitor_->IsPlaying()) {
            // Engine auto-stopped at the end of the timeline (or
            // at t=0 on a reverse shuttle).
            shuttle_rate_ = 0.0;
            monitor_->SetPlaying(false);
            monitor_->SetExternalPlayback(false);
            monitor_->SetTime(playhead_, true);
            play_btn_->SetText("Play");
        }
    }
    // Mirror the engine's transport state onto the monitor's play
    // button (MonitorWidget::SetEnginePlaying is added by the monitor
    // pass — call after pending_playhead_/engine updates above).
    if (monitor_) {
        monitor_->SetEnginePlaying(preview_ready_ &&
                                   preview_engine_.Playing());
    }
    // Source monitor pump — same marshal discipline as the program
    // path (swap pending values out under the lock, apply unlocked).
    if (source_monitor_ != nullptr) {
        std::vector<std::uint8_t> src_frame;
        int src_w = 0, src_h = 0;
        bool has_src = false;
        double src_playhead = -1.0;
        {
            std::lock_guard<std::mutex> lock(source_mutex_);
            if (source_has_frame_) {
                src_frame.swap(source_pending_frame_);
                src_w = source_pending_w_;
                src_h = source_pending_h_;
                source_has_frame_ = false;
                has_src = true;
            }
            if (source_pending_playhead_ >= 0.0) {
                src_playhead = source_pending_playhead_;
                source_pending_playhead_ = -1.0;
            }
        }
        if (has_src) {
            source_monitor_->SetFrame(src_frame, src_w, src_h);
        }
        if (src_playhead >= 0.0) {
            source_playhead_ =
                std::clamp(src_playhead, 0.0, source_duration_);
            source_monitor_->SetTime(source_playhead_);
            if (source_ready_ && !source_engine_.Playing() &&
                source_playing_) {
                // Engine auto-stopped at the end of the media (or at
                // t=0 on a reverse shuttle).
                source_shuttle_rate_ = 0.0;
                source_playing_ = false;
                source_monitor_->SetPlaying(false);
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
            // extract_poster=false — poster decode lands on media_prep.
            if (catalog_.Open(&error) &&
                catalog_.Import(png, &id, false, &error)) {
                RefreshCatalog();
                selected_media_path_ = png;
                media_prep_.RequestImportPoster(png);
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
