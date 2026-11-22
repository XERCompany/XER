#include "ve_app.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "ai/audio_pcm.h"
#include "ai/whisper_transcribe.h"
#include "app_runtime/app_runtime.h"
#include "builtin_fx.h"
#include "captions.h"
#include "cef/cef_ext_scheme.h"
#include "color_panel.h"
#include "effects_panel.h"
#include "export_engine.h"
#include "extensions/ext_host.h"
#include "extensions_panel.h"
#include "history_panel.h"
#include "inspector_panel.h"
#include "js_fx.h"
#include "markers_panel.h"
#include "media_panel.h"
#include "mixer_panel.h"
#include "media_prep.h"
#include "monitor_panel.h"
#include "overlays/toast.h"
#include "photo_workspace.h"
#include "plugin_host.h"
#include "proxy_manager.h"
#include "queue_panel.h"
#include "rendering/renderer.h"
#include "scopes_panel.h"
#include "shell_widgets.h"
#include "timecode.h"
#include "timeline_bridge.h"
#include "timeline_history.h"
#include "timeline_strip.h"
#include "timeline_surface.h"
#include "transcript_edit.h"
#include "transcript_panel.h"
#include "ve_theme.h"
#include "widgets/app_title_bar.h"
#include "widgets/command.h"
#include "widgets/dialog.h"
#include "widgets/resizable.h"
#include "widgets/sidebar.h"
#include "widgets/widget_tree.h"

#include "voice_isolate.h"

#if defined(_WIN32)
#include <windows.h>
// WIN32_LEAN_AND_MEAN keeps these out of <windows.h>:
#include <commdlg.h>   // GetOpenFileNameW / OPENFILENAMEW
#include <shellapi.h>  // ShellExecuteW

#include "helper/path_utils.h"
#include "helper/string_utils.h"
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;

W::Resizable::Panel RP(std::shared_ptr<W::Widget> w, float size,
                       float min_px) {
    return W::Resizable::Panel{std::move(w), size, min_px,
                               /*collapsible=*/true};
}

// M1 placeholder factory — every real surface lands in M2/M3 behind
// the surface_hooks.h contracts.
std::shared_ptr<PlaceholderPanel> PH(const char* id, const char* title,
                                     const char* desc) {
    return PlaceholderPanel::Create(id, title, desc);
}

}  // namespace

VeApp::VeApp(std::string project_path, std::filesystem::path prefs_path)
    : project_path_(std::move(project_path)),
      prefs_path_(prefs_path.empty() ? DefaultShellPrefsPath()
                                     : std::move(prefs_path)) {
    // Restore the last session's shell layout; malformed/missing files
    // fall back to defaults silently (first run).
    LoadShellPrefs(prefs_path_, &prefs_);
    // M2: the shell owns the document. Project + history + bridge
    // must exist before BuildShell wires the surface.
    EnsureTimelineDocument();
    BuildShell();
    BindShellShortcuts();
    ApplyPrefsToShell();
    if (const WorkspaceLayout* ws = FindWorkspace(prefs_.workspace)) {
        ApplyWorkspace(*ws);   // preset wins over raw fields
    }
    SetStatus("ui2 shell — M2 monitors + timeline");
}

VeApp::~VeApp() {
    // Worker teardown: the whisper job touches only its own
    // mutex-guarded slot, but the thread itself must not outlive the
    // members it captures — cancel + join before the fields go.
    transcript_cancel_ = true;
    if (transcript_worker_.joinable()) transcript_worker_.join();
    // export_engine_ tears itself down (ExportEngine::~ExportEngine
    // Cancel()s + Wait()s the worker).
}

// ── structure accessors ──────────────────────────────────────────────

W::AppTitleBar* VeApp::TitleBar() const { return title_bar_.get(); }
W::Sidebar* VeApp::NavRail() const { return nav_rail_.get(); }
DockTabGroup* VeApp::LeftDock() const { return left_dock_.get(); }
DockTabGroup* VeApp::RightDock() const { return right_dock_.get(); }
W::Resizable* VeApp::TopSplit() const { return top_split_.get(); }
W::Resizable* VeApp::OuterSplit() const { return outer_split_.get(); }
W::Resizable* VeApp::MonitorSplit() const { return monitor_split_.get(); }
TimelineStrip* VeApp::Timeline() const { return timeline_.get(); }
StatusStrip* VeApp::Status() const { return status_.get(); }

// ── shell construction ───────────────────────────────────────────────

void VeApp::BuildShell() {
    title_bar_ = std::make_shared<W::AppTitleBar>("XER Studio");
    AddChild(title_bar_);
    BuildTitleBar();

    // Far-left icon rail (collapsed Sidebar — icon column only).
    nav_rail_ = W::Sidebar::Create();
    nav_rail_->SetCollapsed(true);
    nav_rail_->SetItems(
        {{"Edit", "E"}, {"Fx", "F"}, {"Audio", "A", true},
         {"Color", "C"}, {"Markers", "M", true}, {"Photo", "P"},
         {"Lab", "L"}});
    nav_rail_->SetOnSelect([this](int index) {
        // Rail presets — same WorkspaceLayout data as the switcher.
        static const char* kNames[] = {"Editing", "Effects", "Audio",
                                       "Color",    "Markers", "Photo",
                                       "Cut"};
        if (index >= 0 && index < 7) {
            ApplyWorkspaceByName(kNames[index]);
        }
    });
    nav_rail_->SetTooltip("Workspace rail");

    // Dock tab groups — spec §5 order.
    left_dock_ = DockTabGroup::Create();
    right_dock_ = DockTabGroup::Create();
    BuildDockPanels();

    // Monitor region: Resizable Source|Program pair — the M2
    // MonitorPanel chrome (MonitorSurface frame + overlays + ruler,
    // transport row, timecode, quality select, meters) behind the
    // surface_hooks.h contract. Activation routes the ShortcutRouter
    // scope (last-active-viewer rule).
    source_monitor_ =
        MonitorPanel::Create(MonitorPanel::Kind::Source);
    program_monitor_ =
        MonitorPanel::Create(MonitorPanel::Kind::Program);
    WireMonitorPanels();
    monitor_split_ = W::Resizable::Create(W::Resizable::Direction::Horizontal);
    monitor_split_->SetHandleSize(Theme::kSpaceXs);
    monitor_split_->AddPanel(RP(source_monitor_, 0.5f, 160.0f));
    monitor_split_->AddPanel(RP(program_monitor_, 0.5f, 160.0f));
    monitor_split_->SetOnLayout([this](const std::vector<float>& s) {
        prefs_.monitor_sizes = s;
        MarkPrefsDirty();
    });

    top_split_ = W::Resizable::Create(W::Resizable::Direction::Horizontal);
    top_split_->SetHandleSize(Theme::kSpaceXs);
    top_split_->SetOnLayout([this](const std::vector<float>& s) {
        prefs_.top_sizes = s;
        MarkPrefsDirty();
    });

    // Nav rail sits outside the resizable — fixed-width column.
    top_row_ = LinearBox::Create(LinearBox::Direction::Horizontal);
    top_row_->Add(nav_rail_, LinearBox::Slot{Theme::kNavRailWidth, 0.0f});
    top_row_->Add(top_split_, LinearBox::Slot{0.0f, 1.0f});

    timeline_ = TimelineStrip::Create();
    timeline_->SetOnToolChanged([this](int idx) {
        SetStatus("Tool " + std::to_string(idx));
    });
    timeline_->SetOnSnapToggled([this](bool enabled) {
        SetStatus(enabled ? "Snap enabled" : "Snap disabled");
    });
    WireTimelineBridge();

    // M3 panels — hook wiring after the timeline surface exists (the
    // bin's drag handoff + the inspector's selection provider read it).
    WireMediaPanel();
    WireEffectsPanel();
    WireInspectorPanel();
    WireColorPanel();
    WireScopesPanel();
    WireMixerPanel();
    WireMarkersPanel();
    WireHistoryPanel();
    WireQueuePanel();
    WireExtensionsPanel();
    WireTranscriptPanel();

    status_ = StatusStrip::Create();
    status_->SetBadges({{"autosave", "Autosave off"},
                        {"jobs", "0 jobs"},
                        {"proxy", "Proxies idle"}});

    outer_split_ = W::Resizable::Create(W::Resizable::Direction::Vertical);
    outer_split_->SetHandleSize(Theme::kSpaceXs);
    outer_split_->SetOnLayout([this](const std::vector<float>& s) {
        prefs_.outer_sizes = s;
        // Track the timeline share as the "live" fraction so a dragged
        // split survives workspace switches that don't reset it.
        if (s.size() >= 3u && s[2] > 0.0f && s[1] > 0.0f) {
            timeline_fraction_ = static_cast<double>(s[1] / (s[0] + s[1]));
        }
        MarkPrefsDirty();
    });
    outer_split_->AddPanel(RP(top_row_, 0.62f, 240.0f));
    outer_split_->AddPanel(RP(timeline_, 0.34f, 120.0f));
    outer_split_->AddPanel(RP(status_, 0.04f, Theme::kStatusStripHeight));
    AddChild(outer_split_);

    // Command palette — overlay widget, shown via ShowOverlay().
    palette_ = W::Command::Create("Type a command or search…");
    RebuildCommandItems();
}

void VeApp::BuildDockPanels() {
    // M3 wave-1: real panels replace the M1 placeholders for Media,
    // Effects and Inspector. Wave-2 adds Color, Mix and Scopes.
    // Panels are created + hook-wired here; the media stack
    // (catalog/prep/proxies) lazy-inits so smoke construction stays
    // thread-free until the bin needs it.
    media_panel_ = MediaPanel::Create();
    effects_panel_ = EffectsPanel::Create();
    inspector_panel_ = InspectorPanel::Create();
    color_panel_ = ColorPanel::Create();
    scopes_panel_ = ScopesPanel::Create();
    mixer_panel_ = MixerPanel::Create();
    // M3 wave-3 right-dock panels — real surfaces replace the
    // remaining M1 placeholders. Tab order below MUST match the
    // kRightTab* constants in workspace_layout.h.
    markers_panel_ = MarkersPanel::Create();
    history_panel_ = HistoryPanel::Create();
    queue_panel_ = QueuePanel::Create();
    extensions_panel_ = ExtensionsPanel::Create();
    transcript_panel_ = TranscriptPanel::Create();

    left_dock_->AddTab("Media", "Media bin — catalog browser",
                       media_panel_);
    left_dock_->AddTab("Effects", "Effects browser",
                       effects_panel_);
    left_dock_->AddTab("Audio", "Audio tools (M3)",
                       PH("dock.audio", "Audio",
                          "Clip mixer + track controls"));
    left_dock_->AddTab("Text", "Titles & text (M3)",
                       PH("dock.text", "Text", "Title designer"));
    left_dock_->AddTab("Trans", "Transitions browser (M3)",
                       PH("dock.transitions", "Transitions",
                          "Transition presets"));

    right_dock_->AddTab("Inspector", "Clip inspector",
                        inspector_panel_);
    right_dock_->AddTab("Color", "Color workspace — wheels, tint, "
                                 "white balance",
                        color_panel_);
    right_dock_->AddTab("Markers", "Markers list — seek, edit, color",
                        markers_panel_);
    right_dock_->AddTab("History", "Undo history — jump to any state",
                        history_panel_);
    right_dock_->AddTab("Mix", "Audio mixer — track faders, pan, "
                               "meters",
                        mixer_panel_);
    right_dock_->AddTab("Queue", "Export queue — render jobs",
                        queue_panel_);
    right_dock_->AddTab("Ext", "Extensions — xer-ext host",
                        extensions_panel_);
    right_dock_->AddTab("Scopes", "Video scopes — waveform, "
                                  "histogram, more",
                        scopes_panel_);
    right_dock_->AddTab("Trn", "Transcript editor — text-based "
                               "editing",
                        transcript_panel_);
    left_dock_->SetOnChanged([this](int i) {
        prefs_.left_tab = i;
        MarkPrefsDirty();
    });
    right_dock_->SetOnChanged([this](int i) {
        prefs_.right_tab = i;
        MarkPrefsDirty();
    });
}

void VeApp::RebuildOuterSplit() {
    if (top_split_ == nullptr) return;
    const std::vector<float>& saved = prefs_.top_sizes;
    const float l =
        saved.size() == 3u ? saved[0]
                           : (left_visible_ ? 0.22f : 0.0f);
    const float r =
        saved.size() == 3u ? saved[2]
                           : (right_visible_ ? 0.26f : 0.0f);
    const float c =
        saved.size() == 3u
            ? saved[1]
            : std::max(0.2f, 1.0f - l - r);
    top_split_->ClearPanels();
    top_split_->AddPanel(RP(left_dock_, l, 160.0f));
    if (photo_mode_ && photo_workspace_ != nullptr) {
        // "Photo" preset: the photo workspace band takes the center
        // slot the monitor pair otherwise fills.
        top_split_->AddPanel(RP(photo_workspace_, c, 320.0f));
    } else {
        top_split_->AddPanel(RP(monitor_split_, c, 320.0f));
    }
    top_split_->AddPanel(RP(right_dock_, r, 160.0f));
    SyncDockCollapsed();
}

class PhotoWorkspace* VeApp::PhotoWorkspaceView() const {
    return photo_workspace_.get();
}

void VeApp::EnsurePhotoWorkspace() {
    if (photo_workspace_ != nullptr) return;
    PhotoWorkspace::Hooks hooks;
    hooks.on_status = [this](const std::string& text, bool is_error) {
        SetStatus(text, is_error);
    };
    photo_workspace_ = PhotoWorkspace::Create(std::move(hooks));
    photo_workspace_->EnsureDocument();
}

void VeApp::SyncDockCollapsed() {
    if (top_split_ != nullptr) {
        top_split_->SetCollapsed(0, !left_visible_);
        top_split_->SetCollapsed(2, !right_visible_);
    }
    if (outer_split_ != nullptr) {
        outer_split_->SetCollapsed(2, !status_visible_);
        outer_split_->SetCollapsed(1, timeline_fraction_ <= 0.0);
    }
    if (monitor_split_ != nullptr) {
        monitor_split_->SetCollapsed(0, !source_monitor_visible_);
    }
    if (title_bar_ != nullptr) {
        title_bar_->SetLeftPanelToggle(true, left_visible_, [this] {
            SetLeftDockVisible(!left_visible_);
        });
        title_bar_->SetRightPanelToggle(true, right_visible_, [this] {
            SetRightDockVisible(!right_visible_);
        });
    }
}

// ── title bar / menus ────────────────────────────────────────────────

void VeApp::BuildTitleBar() {
    title_bar_->SetWorkspaceLabel(workspace_name_);
    std::vector<W::AppTitleBar::MenuItem> ws_items;
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        ws_items.push_back(W::AppTitleBar::MenuItem{
            ws.name, "",
            [this, name = ws.name] { ApplyWorkspaceByName(name); },
            true, ws.name == workspace_name_});
    }
    title_bar_->SetWorkspaceItems(std::move(ws_items));
    title_bar_->SetCenterCommand(
        "Search or run command…",
        [this] { OpenCommandPalette(); });
    RebuildMenus();
}

void VeApp::RebuildMenus() {
    using Menu = W::AppTitleBar::Menu;
    Menu file{"File", 0, {}};
    file.items = {
        {"New Project", "Ctrl+N",
         [this] { Notify("New Project", "M2 — project lifecycle"); }},
        {"Open Project…", "Ctrl+O",
         [this] { Notify("Open Project", "M2 — project lifecycle"); }},
        {"Save Project", "Ctrl+S",
         [this] { Notify("Save Project", "M2 — project lifecycle"); }},
        {"Import Media…", "",
         [this] { ImportMediaInteractive(); },
         true, false, false, true},
        {"Export…", "",
         [this] { Notify("Export", "M2 — export pipeline"); }},
        {"Open Photo Document…", "",
         [this] {
             ApplyWorkspaceByName("Photo");
             if (photo_workspace_ != nullptr) {
                 photo_workspace_->OpenDialog();
             }
         },
         true, false, false, true},
        {"Save Photo Document", "",
         [this] {
             if (photo_workspace_ != nullptr) {
                 photo_workspace_->Save(/*choose_path=*/false);
             } else {
                 SetStatus("No photo document", true);
             }
         }},
        {"Save Photo As…", "",
         [this] {
             ApplyWorkspaceByName("Photo");
             if (photo_workspace_ != nullptr) {
                 photo_workspace_->Save(/*choose_path=*/true);
             }
         }},
        {"Export Photo PNG…", "",
         [this] {
             ApplyWorkspaceByName("Photo");
             if (photo_workspace_ != nullptr) {
                 photo_workspace_->ExportPng();
             }
         }},
        {"New Photo Document…", "",
         [this] {
             ApplyWorkspaceByName("Photo");
             if (photo_workspace_ != nullptr) {
                 photo_workspace_->NewDocumentDialog();
             }
         },
         true, false, false, true},
    };
    Menu edit{"Edit", 0, {}};
    edit.items = {
        {"Undo", "Ctrl+Z",
         [this] { SetStatus("Undo — M2"); }},
        {"Redo", "Ctrl+Y",
         [this] { SetStatus("Redo — M2"); }},
    };
    Menu view{"View", 0, {}};
    view.items.push_back({"Left Dock", "",
                          [this] {
                              SetLeftDockVisible(!left_visible_);
                          },
                          true, left_visible_});
    view.items.push_back({"Right Dock", "",
                          [this] {
                              SetRightDockVisible(!right_visible_);
                          },
                          true, right_visible_});
    view.items.push_back({"Status Bar", "",
                          [this] {
                              SetStatusVisible(!status_visible_);
                          },
                          true, status_visible_});
    view.items.push_back({"Source Monitor", "",
                          [this] {
                              source_monitor_visible_ =
                                  !source_monitor_visible_;
                              prefs_.source_monitor_visible =
                                  source_monitor_visible_;
                              SyncDockCollapsed();
                              RebuildMenus();
                              MarkPrefsDirty();
                          },
                          true, source_monitor_visible_, false, true});
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        view.items.push_back(
            {"Workspace: " + ws.name, "",
             [this, name = ws.name] { ApplyWorkspaceByName(name); },
             true, ws.name == workspace_name_});
    }
    Menu help{"Help", 0, {}};
    help.items = {
        {"Command Palette", "Ctrl+K",
         [this] { OpenCommandPalette(); }},
        {"About ui2", "",
         [this] {
             ShowDialog("ui2 shell",
                        "M1: layout + docks + router only.\n"
                        "Real surfaces land in M2/M3.");
         }},
    };
    title_bar_->SetMenus({std::move(file), std::move(edit),
                          std::move(view), std::move(help)});
}

// ── command palette / dialogs / toasts ───────────────────────────────

void VeApp::RebuildCommandItems() {
    std::vector<W::Command::Item> items;
    for (const WorkspaceLayout& ws : BuiltinWorkspaces()) {
        items.push_back({"Workspace: " + ws.name, "Layout",
                         [this, name = ws.name] {
                             ApplyWorkspaceByName(name);
                         }});
    }
    items.push_back({"Toggle Left Dock", "View", [this] {
                         SetLeftDockVisible(!left_visible_);
                     }});
    items.push_back({"Toggle Right Dock", "View", [this] {
                         SetRightDockVisible(!right_visible_);
                     }});
    items.push_back({"Toggle Status Bar", "View", [this] {
                         SetStatusVisible(!status_visible_);
                     }});
    items.push_back({"Toggle Source Monitor", "View", [this] {
                         source_monitor_visible_ =
                             !source_monitor_visible_;
                         prefs_.source_monitor_visible =
                             source_monitor_visible_;
                         SyncDockCollapsed();
                         RebuildMenus();
                         MarkPrefsDirty();
                     }});
    for (int i = 0; i < left_dock_->TabCount(); ++i) {
        items.push_back(
            {"Left Dock tab " + std::to_string(i + 1), "Dock",
             [this, i] { left_dock_->SetActiveIndex(i); }});
    }
    for (int i = 0; i < right_dock_->TabCount(); ++i) {
        items.push_back(
            {"Right Dock tab " + std::to_string(i + 1), "Dock",
             [this, i] { right_dock_->SetActiveIndex(i); }});
    }
    items.push_back({"About ui2", "Help", [this] {
                         ShowDialog("ui2 shell",
                                    "M1 placeholder shell.");
                     }});
    palette_->SetItems(std::move(items));
}

void VeApp::OpenCommandPalette() {
    if (palette_ == nullptr || palette_->IsOpen()) return;
    RebuildCommandItems();
    ShowOverlay(palette_);
    palette_->Open();
}

void VeApp::ShowDialog(const std::string& title, const std::string& body,
                       std::function<void()> on_accept) {
    auto content =
        PlaceholderPanel::Create("dialog.body", "", body);
    dialog_ = W::Dialog::Create(title, content, "OK", "Cancel");
    dialog_->SetOnResult(
        [on_accept = std::move(on_accept)](W::Dialog::Result r) {
            if (r == W::Dialog::Result::Accepted && on_accept) {
                on_accept();
            }
        });
    dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(dialog_);
}

void VeApp::Notify(const std::string& title, const std::string& message) {
    XER::UI::Overlays::ToastInfo(title, message);
    SetStatus(title + " — " + message);
}

void VeApp::SetStatus(const std::string& text, bool is_error) {
    if (status_ != nullptr) {
        status_->SetStatusText(is_error ? "Error: " + text : text);
    }
}

// ── M2 document / timeline bridge ────────────────────────────────────
// Mirrors the old shell's ownership: VideoEditorAppWidget owned the
// Project + TimelineHistory and every UI entry point funneled edits
// through history. Here the shell owns the same pair; the timeline
// surface sees only TimelineHooks via TimelineBridge.

void VeApp::EnsureTimelineDocument() {
    if (project_.tracks.empty()) {
        // Legacy "New project" seeds V1+V2 video and A1+A2 audio so
        // the timeline always shows an editable grid.
        Track v1; v1.kind = TrackKind::Video; v1.name = "V1";
        Track v2; v2.kind = TrackKind::Video; v2.name = "V2";
        Track a1; a1.kind = TrackKind::Audio; a1.name = "A1";
        Track a2; a2.kind = TrackKind::Audio; a2.name = "A2";
        project_.tracks = {v1, v2, a1, a2};
    }
    history_ = std::make_unique<TimelineHistory>(project_);
    bridge_ = std::make_unique<TimelineBridge>(project_, *history_);
}

void VeApp::WireTimelineBridge() {
    if (timeline_ == nullptr || bridge_ == nullptr) return;
    const std::shared_ptr<TimelineSurface>& surface =
        timeline_->Surface();
    if (surface == nullptr) return;

    TimelineBridge::Host host;
    host.on_status = [this](const std::string& text, bool is_error) {
        SetStatus(text, is_error);
    };
    host.on_select = [this](const std::string& clip_id) {
        if (const auto& s = timeline_->Surface()) {
            s->SetSelectedClip(clip_id);
        }
        RefreshInspector();   // M3: selection provider re-pull
        RefreshTranscriptSubject();   // wave-3: selection rebinds the subject
    };
    host.on_seek = [this](double t) { OnBridgeSeek(t); };
    host.on_project_changed = [this] { NotifyTimelineChanged(); };
    host.on_snap_toggle = [this](bool enabled) {
        timeline_->SetSnapEnabled(enabled);
    };
    // Media seams (bounds/posters/peaks) stay unwired until the
    // M2 media-prep catalog lands — the bridge's documented
    // fallback is unbounded/no-cache, matching the legacy
    // catalog-miss path.
    bridge_->SetHost(std::move(host));

    surface->SetHooks(bridge_->Hooks());
    surface->SetProject(&project_);
    surface->SetPlayheadSeconds(playhead_);
    NotifyTimelineChanged();
}

void VeApp::NotifyTimelineChanged() {
    // Legacy NotifyProjectChanged: repaint surfaces bound to the
    // document. Playhead re-clamps in case an undo trimmed the
    // tail under the cursor.
    playhead_ = std::clamp(playhead_, 0.0, project_.duration());
    if (timeline_ != nullptr) {
        if (const auto& s = timeline_->Surface()) {
            s->SetProject(&project_);
            s->SetSelectedClip(bridge_ != nullptr
                                   ? bridge_->SelectedClipId()
                                   : std::string{});
            s->SetPlayheadSeconds(playhead_);
        }
    }
    // Push the document fields the program monitor mirrors
    // (duration / work area / markers / fps for the timecode) and
    // mark the engine binding dirty — edits never touch a live
    // session; the next transport action re-inits it.
    if (program_monitor_ != nullptr) {
        program_monitor_->SetDuration(project_.duration());
        program_monitor_->SetFps(project_.fps_num, project_.fps_den);
        program_monitor_->SetWorkArea(project_.work_in,
                                      project_.work_out);
        program_monitor_->SetMarkers(project_.markers);
    }
    program_engine_.SetFps(project_.fps_num, project_.fps_den);
    program_engine_.SetWorkArea(project_.work_in, project_.work_out);
    program_engine_.SetProject(project_);
    // M3: document edits (incl. undo/redo) invalidate the inspector's
    // Clip copies — re-pull through the provider.
    RefreshInspector();
    // Same push for the wave-2 panels: the color target re-pulls
    // (params rebuild only when the target clip changes — wheels keep
    // their view state across doc edits), and the mixer re-syncs its
    // strips against the track layout.
    if (color_panel_ != nullptr) color_panel_->RefreshClip();
    if (mixer_panel_ != nullptr) mixer_panel_->Refresh();
    // Wave-3: marker table + history stacks re-pull on every
    // document mutation (incl. undo/redo + macro boundaries); the
    // transcript subject re-resolves its bound clip (a LiftRange may
    // have split it).
    RefreshMarkersPanel();
    RefreshHistoryPanel();
    RefreshTranscriptSubject();
}

void VeApp::OnBridgeSeek(double seconds) {
    playhead_ = seconds;
    if (timeline_ != nullptr && timeline_->Surface() != nullptr) {
        timeline_->Surface()->SetPlayheadSeconds(seconds);
    }
    // Keep the preview engine on the same playhead — a paused seek
    // presents one frame, a playing seek rebases the wall-clock
    // anchor (old Seek parity). The binding's on_playhead fan-out
    // echoes back through ApplyProgramPlayhead (idempotent).
    program_engine_.Seek(playhead_);
}

void VeApp::Seek(double seconds) {
    if (bridge_ != nullptr) bridge_->Seek(seconds);
}

void VeApp::Undo() {
    if (history_ == nullptr) return;
    if (history_->Undo()) {
        NotifyTimelineChanged();
        SetStatus("Undo");
    } else {
        SetStatus("Nothing to undo");
    }
}

void VeApp::Redo() {
    if (history_ == nullptr) return;
    if (history_->Redo()) {
        NotifyTimelineChanged();
        SetStatus("Redo");
    } else {
        SetStatus("Nothing to redo");
    }
}

// ── M2 monitors / playback ───────────────────────────────────────────
// Mirrors ui/video_editor_app.cc: the shell owns the PreviewEngine
// pair (via PreviewBinding), enforces the exclusive-playback rule
// (one engine owns the audio endpoint), routes the last-active-viewer
// scope through ShortcutRouter::ActivateScope, and marshals worker
// callbacks through DrainPending() in Tick.

void VeApp::WireMonitorPanels() {
    if (program_monitor_ == nullptr || source_monitor_ == nullptr) {
        return;
    }
    program_engine_.AttachPanel(program_monitor_.get());
    source_engine_.AttachPanel(source_monitor_.get());
    program_engine_.SetOnPlayhead(
        [this](double t) { ApplyProgramPlayhead(t); });
    source_engine_.SetOnPlayhead(
        [this](double t) { ApplySourcePlayhead(t); });
    const auto status = [this](const std::string& s, bool err) {
        SetStatus(s);
        if (err) Notify("Monitor", s);
    };
    program_engine_.SetOnStatus(status);
    source_engine_.SetOnStatus(status);

    MonitorPanel::Hooks ph;
    ph.on_activate = [this] {
        shortcuts_.ActivateScope(ShortcutRouter::kScopeProgram);
    };
    ph.on_play_toggle = [this] { ProgramTogglePlay(); };
    ph.on_stop = [this] { ProgramShuttleStop(); };
    ph.on_frame_step = [this](int dir) { ProgramStepFrame(dir); };
    ph.on_to_start = [this] { Seek(0.0); };
    ph.on_to_end = [this] { Seek(project_.duration()); };
    ph.on_seek = [this](double t) { Seek(t); };
    ph.on_quality = [this](float scale) {
        program_engine_.SetQuality(scale);
    };
    ph.on_safe_toggled = [this](bool on) {
        program_monitor_->SetSafeMargins(on);
        SetStatus(on ? "Safe margins on" : "Safe margins off");
    };
    ph.on_grid_toggled = [this](bool on) {
        program_monitor_->SetGrid(on);
        SetStatus(on ? "Grid on" : "Grid off");
    };
    ph.on_loop_toggled = [this](bool on) {
        program_engine_.SetLoopEnabled(on);
        program_monitor_->SetLoopEnabled(on);
        SetStatus(on ? "Program loop on" : "Program loop off");
    };
    ph.on_mark_in = [this] { ProgramSetWorkAreaPoint(true); };
    ph.on_mark_out = [this] { ProgramSetWorkAreaPoint(false); };
    ph.on_lift = [this] { ProgramLiftExtract(false); };
    ph.on_extract = [this] { ProgramLiftExtract(true); };
    // Armed click points: the color panel's white-balance eyedropper
    // owns the seam for now (SmartSelect/lab arming lands with the
    // lab wave — both can't be armed at once, first writer wins).
    ph.on_click_point = [this](float nx, float ny) {
        if (!color_pick_armed_) return;
        color_pick_armed_ = false;
        program_monitor_->SetClickPointsArmed(false);
        if (color_panel_ == nullptr) return;
        const auto frame =
            program_monitor_->VideoSurface().CurrentFrame();
        if (frame.bgra == nullptr) {
            color_panel_->CancelPick();
            SetStatus("No program frame to sample", true);
            return;
        }
        color_panel_->ApplyPickedColor(
            SampleProgramFrame(nx, ny));
    };
    ph.on_status = status;
    program_monitor_->SetHooks(std::move(ph));

    MonitorPanel::Hooks sh;
    sh.on_activate = [this] {
        shortcuts_.ActivateScope(ShortcutRouter::kScopeSource);
    };
    sh.on_play_toggle = [this] { SourceTogglePlay(); };
    sh.on_stop = [this] { SourceShuttleStop(); };
    sh.on_frame_step = [this](int dir) { SourceStepFrame(dir); };
    sh.on_to_start = [this] { SourceSeek(0.0); };
    sh.on_to_end = [this] { SourceSeek(source_duration_); };
    sh.on_seek = [this](double t) { SourceSeek(t); };
    sh.on_quality = [this](float scale) {
        source_engine_.SetQuality(scale);
    };
    sh.on_safe_toggled = [this](bool on) {
        source_monitor_->SetSafeMargins(on);
        SetStatus(on ? "Safe margins on" : "Safe margins off");
    };
    sh.on_grid_toggled = [this](bool on) {
        source_monitor_->SetGrid(on);
        SetStatus(on ? "Grid on" : "Grid off");
    };
    sh.on_loop_toggled = [this](bool on) {
        source_engine_.SetLoopEnabled(on);
        source_monitor_->SetLoopEnabled(on);
        SetStatus(on ? "Source loop on" : "Source loop off");
    };
    sh.on_mark_in = [this] { SourceMark(true); };
    sh.on_mark_out = [this] { SourceMark(false); };
    sh.on_insert = [this] { EditSourceToTimeline(true); };
    sh.on_overwrite = [this] { EditSourceToTimeline(false); };
    sh.on_status = status;
    source_monitor_->SetHooks(std::move(sh));

    // Initial pushed state — the document mirror fields.
    NotifyTimelineChanged();
}

void VeApp::ApplyProgramPlayhead(double seconds) {
    playhead_ = std::clamp(seconds, 0.0, project_.duration());
    if (timeline_ != nullptr) {
        if (const auto& s = timeline_->Surface()) {
            s->SetPlayheadSeconds(playhead_);
        }
    }
}

void VeApp::ApplySourcePlayhead(double seconds) {
    source_playhead_ =
        std::clamp(seconds, 0.0, std::max(0.0, source_duration_));
}

void VeApp::SetProject(const Project& project) {
    // TimelineHistory load contract: Clear() before rebinding the
    // document so pending commands can't restore pre-load clips.
    if (history_ != nullptr) history_->Clear();
    project_ = project;
    playhead_ = 0.0;
    NotifyTimelineChanged();
}

void VeApp::OpenInSourceMonitor(const std::string& path,
                                const std::string& name,
                                double duration_seconds, int fps_num,
                                int fps_den, bool has_video,
                                bool has_audio, int media_w,
                                int media_h) {
    if (path.empty()) return;
    if (!has_video && !has_audio) {
        SetStatus("Media has no decodable streams: " + name, true);
        return;
    }
    // Ported from VideoEditorAppWidget::OpenInSourceMonitor — the
    // caller (M3's bin) supplies the probed metadata; here we stop
    // the old session, build the synthetic one-clip project the
    // source engine decodes, restore persisted marks, and reveal +
    // activate the viewer.
    StopSourceTransport();
    source_engine_.Shutdown();
    source_path_ = path;
    source_name_ = name;
    source_duration_ = duration_seconds > 0.0
                           ? duration_seconds
                           : kSourceFallbackSeconds;
    source_media_w_ = media_w;
    source_media_h_ = media_h;
    source_has_video_ = has_video;
    source_has_audio_ = has_audio;
    source_fps_num_ = fps_num > 0
                          ? fps_num
                          : (project_.fps_num > 0 ? project_.fps_num
                                                  : 30);
    source_fps_den_ = fps_den > 0 ? fps_den : 1;

    source_project_ = Project{};
    source_project_.width = has_video ? media_w : project_.width;
    source_project_.height = has_video ? media_h : project_.height;
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
    clip.duration = source_duration_;
    clip.transform =
        has_video ? FitTransformToCanvas(media_w, media_h,
                                         media_w, media_h)
                  : Transform{};
    source_project_.clips.push_back(clip);

    // Initial playhead honors a persisted in mark (Premiere drops
    // the source playhead on the mark when a marked clip reopens).
    double initial = 0.0;
    SourceMarks marks;
    if (const auto it = source_marks_.find(source_path_);
        it != source_marks_.end()) {
        marks = it->second;
        if (marks.HasIn()) initial = marks.in;
    }
    source_playhead_ =
        std::clamp(initial, 0.0, source_duration_);

    source_engine_.SetProject(source_project_);
    source_engine_.SetFps(source_fps_num_, source_fps_den_);
    source_engine_.Seek(source_playhead_);
    if (source_monitor_ != nullptr) {
        source_monitor_->SetTitle("SOURCE · " + source_name_);
        source_monitor_->SetDuration(source_duration_);
        source_monitor_->SetFps(source_fps_num_, source_fps_den_);
        source_monitor_->SetMarks(marks.in, marks.out);
        source_monitor_->SetPlaying(false);
        source_monitor_->SetTimecode(source_playhead_);
        source_monitor_->SetStatusText(
            has_video ? "Decoding..." : "Audio only");
    }
    source_monitor_visible_ = true;
    SyncDockCollapsed();
    RebuildMenus();
    shortcuts_.ActivateScope(ShortcutRouter::kScopeSource);
    SetStatus("Source: " + source_name_);
}

void VeApp::ProgramTogglePlay() {
    // Exclusive playback — one engine owns the audio endpoint (old
    // StopSourceTransport parity).
    if (!program_engine_.Playing()) StopSourceTransport();
    program_engine_.TogglePlay();
}

void VeApp::ProgramShuttleStep(int direction) {
    if (!program_engine_.Playing()) StopSourceTransport();
    program_engine_.ShuttleStep(direction);
}

void VeApp::ProgramShuttleStop() { program_engine_.ShuttleStop(); }

void VeApp::ProgramStepFrame(int direction) {
    if (program_engine_.Playing()) program_engine_.Stop();
    const double frame_dt =
        static_cast<double>(std::max(1, project_.fps_den)) /
        std::max(1, project_.fps_num);
    Seek(playhead_ + direction * frame_dt);
}

void VeApp::ProgramSetWorkAreaPoint(bool mark_in) {
    // Old parity: I/O keys write the work-area edge at the playhead;
    // the model resets the span to "whole" when out <= in.
    if (bridge_ == nullptr) return;
    if (mark_in) {
        bridge_->SetWorkAreaRange(playhead_, project_.work_out);
    } else {
        bridge_->SetWorkAreaRange(project_.work_in, playhead_);
    }
}

void VeApp::ProgramLiftExtract(bool close_gap) {
    const double wi = project_.work_in;
    const double wo = project_.work_out;
    if (wo <= wi) {
        SetStatus("Lift/extract needs a work area (I/O)", true);
        return;
    }
    if (history_ == nullptr) return;
    std::string error;
    if (!history_->LiftRange(wi, wo, close_gap, &error)) {
        SetStatus("Lift/extract failed: " + error, true);
        return;
    }
    NotifyTimelineChanged();
    SetStatus(close_gap ? "Extracted range" : "Lifted range");
}

void VeApp::StopProgramTransport() {
    if (!program_engine_.Playing() &&
        program_engine_.ShuttleRate() == 0.0) {
        return;
    }
    program_engine_.ShuttleStop();
}

void VeApp::SourceTogglePlay() {
    if (source_path_.empty()) {
        SetStatus("No source clip loaded");
        return;
    }
    if (!source_engine_.Playing()) StopProgramTransport();
    source_engine_.TogglePlay();
}

void VeApp::SourceShuttleStep(int direction) {
    if (source_path_.empty()) {
        SetStatus("Source: no clip loaded");
        return;
    }
    if (!source_engine_.Playing()) StopProgramTransport();
    source_engine_.ShuttleStep(direction);
}

void VeApp::SourceShuttleStop() { source_engine_.ShuttleStop(); }

void VeApp::SourceSeek(double seconds) {
    source_engine_.Seek(seconds);
    source_playhead_ = source_engine_.Playhead();
}

void VeApp::SourceStepFrame(int direction) {
    if (source_engine_.Playing()) source_engine_.Stop();
    source_engine_.StepFrame(direction);
    source_playhead_ = source_engine_.Playhead();
}

void VeApp::SourceMark(bool mark_in) {
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

void VeApp::SourceClearMark(bool mark_in) {
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

void VeApp::StopSourceTransport() {
    if (!source_engine_.Playing() &&
        source_engine_.ShuttleRate() == 0.0) {
        return;
    }
    source_engine_.ShuttleStop();
}

void VeApp::EditSourceToTimeline(bool insert) {
    // Ported from VideoEditorAppWidget::EditSourceToTimeline — same
    // mark resolution, target-track rule, linked A/V pair,
    // straddling split and history macro.
    if (source_path_.empty()) {
        SetStatus("No clip loaded in the source monitor", true);
        return;
    }
    if (history_ == nullptr) return;
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

    const TrackKind wanted =
        source_has_video_ ? TrackKind::Video : TrackKind::Audio;
    // Premiere-style A/V edit: a source carrying both streams edits
    // as a linked pair — video on the video track, audio on an
    // audio track, shared link_group.
    const bool linked_av = source_has_video_ && source_has_audio_;
    const std::string selected =
        bridge_ != nullptr ? bridge_->SelectedClipId() : std::string{};
    int track_index = -1;
    if (const Clip* sel = FindClip(project_, selected);
        sel != nullptr && sel->track >= 0 &&
        sel->track < static_cast<int>(project_.tracks.size()) &&
        project_.tracks[sel->track].kind == wanted &&
        !project_.tracks[sel->track].locked) {
        track_index = sel->track;
    }
    if (track_index < 0) {
        for (int i = 0;
             i < static_cast<int>(project_.tracks.size()); ++i) {
            const Track& t = project_.tracks[i];
            if (t.kind == wanted && !t.locked) {
                track_index = i;
                break;
            }
        }
    }
    if (track_index < 0) {
        track_index = history_->AddTrack(
            wanted, wanted == TrackKind::Video ? "Video" : "Audio");
        if (track_index < 0) {
            SetStatus("Source edit failed: cannot add a track", true);
            return;
        }
    }
    int audio_track = -1;
    if (linked_av) {
        for (int i = 0;
             i < static_cast<int>(project_.tracks.size()); ++i) {
            const Track& t = project_.tracks[i];
            if (t.kind == TrackKind::Audio && !t.locked) {
                audio_track = i;
                break;
            }
        }
        if (audio_track < 0) {
            audio_track =
                history_->AddTrack(TrackKind::Audio, "Audio");
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
    // non-overlap validation passes.
    const bool snap =
        timeline_ != nullptr && timeline_->SnapEnabled();
    double at = snap ? SnapTime(project_, track_index, playhead_, 0.1,
                                playhead_)
                     : playhead_;
    at = std::max(0.0, at);
    const auto straddler_on = [this, at](int track) -> const Clip* {
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
    const MediaBounds audio_bounds{source_duration_, false};
    std::string error;

    history_->BeginMacro(insert ? "Insert edit" : "Overwrite edit");
    bool applied_any = false;
    bool ok = true;
    for (const Clip* s : {straddler, audio_straddler}) {
        if (s == nullptr) continue;
        std::string ignored;
        ok = history_->SplitClip(s->id, at, &ignored);
        applied_any |= ok;
        if (!ok) {
            error = "straddling split failed";
            break;
        }
    }
    if (ok) {
        std::string new_id;
        if (insert) {
            ok = history_->InsertClipAt(clip, bounds, &new_id, &error);
        } else {
            ok = history_->OverwriteClipAt(clip, bounds, &new_id,
                                           &error);
        }
        applied_any |= ok;
        if (ok) clip.id = new_id;
    }
    if (ok && linked_av) {
        std::string new_id;
        if (insert) {
            ok = history_->InsertClipAt(audio_clip, audio_bounds,
                                        &new_id, &error);
        } else {
            ok = history_->OverwriteClipAt(audio_clip, audio_bounds,
                                           &new_id, &error);
        }
        applied_any |= ok;
    }
    history_->EndMacro();
    if (!ok) {
        // Undo whatever the macro applied (typically the straddling
        // split) so a failed edit leaves the timeline untouched.
        if (applied_any) history_->Undo();
        SetStatus("Source edit failed: " + error, true);
        return;
    }

    if (bridge_ != nullptr) bridge_->SelectClip(clip.id);
    Seek(clip.timeline_start + clip.duration);
    NotifyTimelineChanged();
    SetStatus(std::string(insert ? "Inserted " : "Overwrote ") +
              source_name_);
}

// ── shortcuts ────────────────────────────────────────────────────────

void VeApp::InstallShortcutRouter() {
    if (shortcuts_installed_ || GetTree() == nullptr) return;
    shortcuts_installed_ = true;
    GetTree()->SetKeyHandler(
        [this](const W::Event& e) { return shortcuts_.HandleEvent(e); });
}

void VeApp::BindShellShortcuts() {
    auto status = [this](std::string s) {
        return [this, s = std::move(s)] {
            SetStatus(s);
            return true;
        };
    };
    using R = ShortcutRouter;
    // J/K/L transport — bound per viewer scope; ActivateScope follows
    // MonitorHooks::on_activate (last-active-viewer rule). The
    // PreviewBinding pair carries the ±{1,2,4,8} ladder.
    shortcuts_.BindKey('j', [this] {
                           SourceShuttleStep(-1);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey('k', [this] {
                           SourceShuttleStop();
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey('l', [this] {
                           SourceShuttleStep(1);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey('j', [this] {
                           ProgramShuttleStep(-1);
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindKey('k', [this] {
                           ProgramShuttleStop();
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindKey('l', [this] {
                           ProgramShuttleStep(1);
                           return true;
                       },
                       R::kScopeProgram);
    // I/O: source marks vs program work-area edges (old
    // SourceActive() routing lives in the scope dispatch now).
    shortcuts_.BindKey('i', [this] {
                           SourceMark(true);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey('o', [this] {
                           SourceMark(false);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindAltKey('i', [this] {
                           SourceClearMark(true);
                           return true;
                       },
                          R::kScopeSource);
    shortcuts_.BindAltKey('o', [this] {
                           SourceClearMark(false);
                           return true;
                       },
                          R::kScopeSource);
    shortcuts_.BindKey('i', [this] {
                           ProgramSetWorkAreaPoint(true);
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindKey('o', [this] {
                           ProgramSetWorkAreaPoint(false);
                           return true;
                       },
                       R::kScopeProgram);
    // Three-point edits / range edits + per-scope Space/arrows. An
    // unscoped press falls through to the global program bindings
    // below — same default the old monitor_target_ routing had.
    shortcuts_.BindKey(',', [this] {
                           EditSourceToTimeline(true);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey('.', [this] {
                           EditSourceToTimeline(false);
                           return true;
                       },
                       R::kScopeSource);
    shortcuts_.BindKey(',', status("Clip nudge — M3"),
                       R::kScopeProgram);
    shortcuts_.BindKey('.', status("Clip nudge — M3"),
                       R::kScopeProgram);
    shortcuts_.BindKey(';', [this] {
                           ProgramLiftExtract(false);
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindKey('\'', [this] {
                           ProgramLiftExtract(true);
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindChord(W::KeyCode::Space,
                         [this] {
                             SourceTogglePlay();
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeSource);
    shortcuts_.BindChord(W::KeyCode::Space,
                         [this] {
                             ProgramTogglePlay();
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeProgram);
    shortcuts_.BindChord(W::KeyCode::ArrowLeft,
                         [this] {
                             SourceStepFrame(-1);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeSource);
    shortcuts_.BindChord(W::KeyCode::ArrowRight,
                         [this] {
                             SourceStepFrame(1);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeSource);
    shortcuts_.BindChord(W::KeyCode::ArrowLeft,
                         [this] {
                             ProgramStepFrame(-1);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeProgram);
    shortcuts_.BindChord(W::KeyCode::ArrowRight,
                         [this] {
                             ProgramStepFrame(1);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeProgram);
    shortcuts_.BindChord(W::KeyCode::Home,
                         [this] {
                             SourceSeek(0.0);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeSource);
    shortcuts_.BindChord(W::KeyCode::End,
                         [this] {
                             SourceSeek(source_duration_);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeSource);
    shortcuts_.BindChord(W::KeyCode::Home,
                         [this] {
                             Seek(0.0);
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeProgram);
    shortcuts_.BindChord(W::KeyCode::End,
                         [this] {
                             Seek(project_.duration());
                             return true;
                         },
                         /*ctrl=*/false, R::kScopeProgram);
    // Tool letters → the strip's Tabs row (Premiere letters).
    static const struct { char key; int tool; } kToolKeys[] = {
        {'v', 0}, {'c', 1}, {'b', 2}, {'n', 3}, {'y', 4},
        {'u', 5}, {'r', 6}, {'h', 7}, {'z', 8},
    };
    for (const auto& t : kToolKeys) {
        shortcuts_.BindKey(t.key, [this, idx = t.tool] {
            timeline_->SetTool(idx);
            SetStatus("Tool " + std::to_string(idx) + " (M2)");
            return true;
        });
    }
    shortcuts_.BindKey('m', [this] {
        if (bridge_ != nullptr) {
            bridge_->Hooks().on_add_marker(playhead_, "Marker");
        }
        return true;
    });
    // Shift+M / PageDown jump to the next marker; PageUp previous.
    auto jump_marker = [this](int dir) {
        double best = dir > 0 ? 1e30 : -1e30;
        for (const Marker& m : project_.markers) {
            if (dir > 0 && m.time > playhead_ + 1e-6 &&
                m.time < best) {
                best = m.time;
            }
            if (dir < 0 && m.time < playhead_ - 1e-6 &&
                m.time > best) {
                best = m.time;
            }
        }
        if (best > -1e29 && best < 1e29) {
            Seek(best);
        } else {
            SetStatus(dir > 0 ? "No marker ahead" : "No marker behind");
        }
        return true;
    };
    shortcuts_.BindShiftKey('m', [jump_marker] { return jump_marker(1); });
    shortcuts_.BindKey('s', [this] {
        timeline_->SetSnapEnabled(!timeline_->SnapEnabled());
        if (timeline_->SnapEnabled()) {
            SetStatus("Snap enabled");
        } else {
            SetStatus("Snap disabled");
        }
        return true;
    });
    // Unscoped transport defaults to the program viewer (the
    // last-active rule's "no viewer ever clicked" fallback).
    shortcuts_.BindChord(W::KeyCode::Space,
                         [this] {
                             ProgramTogglePlay();
                             return true;
                         });
    shortcuts_.BindChord(W::KeyCode::ArrowLeft,
                         [this] {
                             ProgramStepFrame(-1);
                             return true;
                         });
    shortcuts_.BindChord(W::KeyCode::ArrowRight,
                         [this] {
                             ProgramStepFrame(1);
                             return true;
                         });
    shortcuts_.BindChord(W::KeyCode::Home, [this] {
        Seek(0.0);
        return true;
    });
    shortcuts_.BindChord(W::KeyCode::End, [this] {
        Seek(project_.duration());
        return true;
    });
    shortcuts_.BindChord(W::KeyCode::PageDown,
                         [jump_marker] { return jump_marker(1); });
    shortcuts_.BindChord(W::KeyCode::PageUp,
                         [jump_marker] { return jump_marker(-1); });
    shortcuts_.BindChord(W::KeyCode::Delete, [this] {
        if (bridge_ != nullptr) {
            bridge_->Hooks().on_delete_clip(bridge_->SelectedClipId());
        }
        return true;
    });
    shortcuts_.BindChord(W::KeyCode::F5,
                         [this] {
                             if (program_engine_.Playing()) {
                                 ProgramShuttleStop();
                             } else {
                                 ProgramTogglePlay();
                             }
                             return true;
                         });
    // App chords — palette opens for real, the rest land with M2.
    shortcuts_.BindChord(W::KeyCode::K,
                         [this] {
                             OpenCommandPalette();
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::P,
                         [this] {
                             OpenCommandPalette();
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::S,
                         status("Save project (M2)"), /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::O,
                         status("Open project (M2)"), /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::N,
                         status("New project (M2)"), /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::Z, [this] {
                             Undo();
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::Y, [this] {
                             Redo();
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::D,
                         status("Default transition (M2)"),
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::Slash,
                         [this] {
                             ShowDialog(
                                 "Shortcuts",
                                 "J/K/L shuttle · I/O marks · "
                                 "V/C/B/N/Y/U/R/H/Z tools · "
                                 "M marker · S snap · Space play");
                             return true;
                         },
                         /*ctrl=*/true);
}

// ── workspaces / docks ───────────────────────────────────────────────

void VeApp::ApplyWorkspace(const WorkspaceLayout& ws) {
    workspace_name_ = ws.name;
    left_visible_ = ws.left_visible;
    right_visible_ = ws.right_visible;
    timeline_fraction_ = ws.timeline_fraction;
    monitors_tabbed_ = ws.monitors_tabbed;
    if (left_dock_ != nullptr && ws.left_tab >= 0 &&
        ws.left_tab < left_dock_->TabCount()) {
        left_dock_->SetActiveIndex(ws.left_tab);
    }
    if (right_dock_ != nullptr && ws.right_tab >= 0 &&
        ws.right_tab < right_dock_->TabCount()) {
        right_dock_->SetActiveIndex(ws.right_tab);
    }
    if (title_bar_ != nullptr) {
        title_bar_->SetWorkspaceLabel(ws.name);
    }
    // "Photo" swaps the top split's center for the photo band — the
    // same slot the monitor pair fills in every other preset.
    photo_mode_ = ws.name == "Photo";
    if (photo_mode_) EnsurePhotoWorkspace();
    RebuildOuterSplit();
    // Rebuild the outer split so the preset's timeline share applies.
    // Resizable has no post-hoc size setter; ClearPanels + AddPanel
    // re-registers the same child widgets with new fractions. The
    // fraction is measured over body+timeline (status stays fixed) —
    // same normalization the drag handler in BuildShell uses.
    if (outer_split_ != nullptr) {
        const float st = 0.045f;
        const float tl = static_cast<float>(timeline_fraction_) *
                         (1.0f - st);
        outer_split_->ClearPanels();
        outer_split_->AddPanel(
            RP(top_row_, std::max(0.0f, 1.0f - st - tl), 240.0f));
        outer_split_->AddPanel(RP(timeline_, tl, 120.0f));
        outer_split_->AddPanel(
            RP(status_, st, Theme::kStatusStripHeight));
    }
    SyncDockCollapsed();
    RebuildMenus();
    CapturePrefsFromSplits();
    prefs_.workspace = workspace_name_;
    MarkPrefsDirty();
    SetStatus("Workspace: " + workspace_name_);
    Invalidate();
    RequestLayout();
}

bool VeApp::ApplyWorkspaceByName(const std::string& name) {
    const WorkspaceLayout* ws = FindWorkspace(name);
    if (ws == nullptr) return false;
    ApplyWorkspace(*ws);
    return true;
}

void VeApp::SetLeftDockVisible(bool visible) {
    left_visible_ = visible;
    prefs_.left_visible = visible;
    SyncDockCollapsed();
    RebuildMenus();
    MarkPrefsDirty();
}

void VeApp::SetRightDockVisible(bool visible) {
    right_visible_ = visible;
    prefs_.right_visible = visible;
    SyncDockCollapsed();
    RebuildMenus();
    MarkPrefsDirty();
}

void VeApp::SetStatusVisible(bool visible) {
    status_visible_ = visible;
    prefs_.status_visible = visible;
    SyncDockCollapsed();
    RebuildMenus();
    MarkPrefsDirty();
}

// ── persistence ──────────────────────────────────────────────────────

void VeApp::MarkPrefsDirty() {
    prefs_dirty_ = true;
    prefs_flush_timer_ = 0.0;
    CapturePrefsFromSplits();
}

void VeApp::CapturePrefsFromSplits() {
    prefs_.workspace = workspace_name_;
    prefs_.left_visible = left_visible_;
    prefs_.right_visible = right_visible_;
    prefs_.status_visible = status_visible_;
    prefs_.source_monitor_visible = source_monitor_visible_;
    prefs_.monitors_tabbed = monitors_tabbed_;
    if (left_dock_ != nullptr) prefs_.left_tab = left_dock_->ActiveIndex();
    if (right_dock_ != nullptr) {
        prefs_.right_tab = right_dock_->ActiveIndex();
    }
}

void VeApp::ApplyPrefsToShell() {
    workspace_name_ = prefs_.workspace.empty() ? "Editing"
                                             : prefs_.workspace;
    left_visible_ = prefs_.left_visible;
    right_visible_ = prefs_.right_visible;
    status_visible_ = prefs_.status_visible;
    source_monitor_visible_ = prefs_.source_monitor_visible;
    monitors_tabbed_ = prefs_.monitors_tabbed;
    if (left_dock_ != nullptr && prefs_.left_tab >= 0 &&
        prefs_.left_tab < left_dock_->TabCount()) {
        left_dock_->SetActiveIndex(prefs_.left_tab);
    }
    if (right_dock_ != nullptr && prefs_.right_tab >= 0 &&
        prefs_.right_tab < right_dock_->TabCount()) {
        right_dock_->SetActiveIndex(prefs_.right_tab);
    }
    RebuildOuterSplit();
    SyncDockCollapsed();
    RebuildMenus();
}

std::string VeApp::FlushPrefs() {
    CapturePrefsFromSplits();
    std::string error;
    if (!SaveShellPrefs(prefs_path_, prefs_, &error)) {
        return error;
    }
    prefs_dirty_ = false;
    return {};
}

// ── widget contract ──────────────────────────────────────────────────

void VeApp::Render(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens) {
    // Canvas fill behind the shell children.
    renderer.FillRect(GetBounds(), tokens.background, 0.0f);
}

void VeApp::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    InstallShortcutRouter();
    const float title_h = Theme::kTitleBarHeight;
    if (title_bar_ != nullptr) {
        const XER::UI::Layout::Rect tb{bounds.x, bounds.y, bounds.width,
                                       title_h};
        title_bar_->Layout(tb, CachedLayoutTokens());
        title_bar_->SetBounds(tb);
    }
    if (outer_split_ != nullptr) {
        const XER::UI::Layout::Rect body{
            bounds.x, bounds.y + title_h, bounds.width,
            std::max(0.0f, bounds.height - title_h)};
        outer_split_->Layout(body, CachedLayoutTokens());
        outer_split_->SetBounds(body);
    }
}

bool VeApp::HandleEvent(const XER::UI::Widgets::Event&) {
    // Shell-level input arrives via the tree's key handler (the router)
    // or direct child hit-testing — nothing else to claim here.
    return false;
}

void VeApp::Tick(double dt_seconds) {
    // Worker→UI marshal: swap the engines' pending frame/peaks/
    // playhead slots onto the panels (old ShellTick parity — the
    // engine callbacks never touch widgets).
    program_engine_.DrainPending();
    source_engine_.DrainPending();
    // Media-prep drains: poster/peaks results repaint the bin
    // (NotifyMediaChanged clears negative caches + invalidates), an
    // import poster writing poster_path refreshes the row data.
    if (media_prep_ != nullptr) {
        if (media_prep_->ConsumeRenderDirty() &&
            media_panel_ != nullptr) {
            media_panel_->NotifyMediaChanged();
        }
        if (media_prep_->ConsumeCatalogDirty()) {
            RefreshBinEntries();
        }
        const std::uint64_t dropped =
            media_prep_->ConsumeDroppedPosterRequests();
        if (dropped > 0) {
            SetStatus("Media queue full — " +
                          std::to_string(dropped) +
                          " poster request(s) dropped",
                      true);
        }
    }
    // Wave-3 drivers: the export engine's Progress()/Status() drain
    // into queue rows, the whisper worker's result slot into the
    // transcript panel + cache. Both are poll-seams — the workers
    // never touch widgets.
    PumpExportQueue(dt_seconds);
    PumpTranscriptJob();
    // Playhead fan-out: the transcript's live-word highlight and the
    // markers table's proximity accent read the shell-owned time.
    if (transcript_panel_ != nullptr) {
        transcript_panel_->SetPlayhead(playhead_);
    }
    if (markers_panel_ != nullptr) {
        markers_panel_->SetPlayhead(playhead_);
    }
    if (prefs_dirty_) {
        prefs_flush_timer_ += dt_seconds;
        if (prefs_flush_timer_ >= 1.0) {
            FlushPrefs();
        }
    }
}

void VeApp::TickTree(double dt_seconds) {
    // The host's frame callback drives us; WidgetTree::Tick only
    // covers the tooltip layer — the widget subtree (VeApp::Tick's
    // DrainPending marshal + prefs flush, MonitorSurface's click
    // clock, LevelMeter ballistics, TimelineStrip's surface re-sync)
    // needs the Widget::TickTree walk (the old shell's TickTree
    // parity — VideoEditorAppWidget inherited Widget::TickTree).
    Widget::TickTree(dt_seconds);
    if (auto* tree = GetTree(); tree != nullptr) {
        tree->Tick(dt_seconds);
    }
}

// ── M3 panels / media stack ──────────────────────────────────────────
// Same ownership rules as M2: VeApp owns the MediaCatalog, the
// MediaPrep worker pool and the ProxyManager; the three M3 panels see
// only their Hooks bundles (media bin: catalog rows + tri-state
// lookups + file actions; effects: registry catalog + apply hook;
// inspector: selection provider + commit hook). No panel mutates
// Project — every document edit funnels through history_ below.

void VeApp::EnsureMediaStack() {
    if (media_stack_ready_) return;
    media_stack_ready_ = true;
    if (catalog_ == nullptr) {
        catalog_ = std::make_unique<MediaCatalog>();
    }
    std::string error;
    if (!catalog_->IsOpen() && !catalog_->Open(&error)) {
        if (!media_stack_warned_) {
            media_stack_warned_ = true;
            SetStatus("Media catalog unavailable: " + error, true);
        }
        return;
    }
    if (catalog_->IsOpen() && media_prep_ == nullptr) {
        // Borrowing order: proxies_ first so the poster path_resolver
        // can route decodes through ready proxy files (the documented
        // Options::path_resolver seam — proxies carry no audio so
        // peaks always hit the original).
        proxies_ = std::make_unique<ProxyManager>(catalog_.get());
        MediaPrep::Options options;
        options.path_resolver =
            [this](const std::string& path) -> std::string {
            return proxies_ != nullptr
                       ? proxies_->ResolveDecodePath(path)
                       : path;
        };
        media_prep_ = std::make_unique<MediaPrep>(
            catalog_.get(), options, MediaPrep::JobRunner{});
    }
}

const CatalogEntry* VeApp::FindCatalogEntry(
    const std::string& path) const {
    if (catalog_ == nullptr || !catalog_->IsOpen()) return nullptr;
    static thread_local CatalogEntry entry;
    if (catalog_->FindByPath(path, &entry)) return &entry;
    return nullptr;
}

void VeApp::RefreshBinEntries() {
    if (media_panel_ == nullptr) return;
    std::vector<CatalogEntry> entries;
    if (catalog_ != nullptr && catalog_->IsOpen()) {
        std::string error;
        entries = catalog_->List(&error);
        if (!error.empty()) {
            SetStatus("Catalog read failed: " + error, true);
        }
    }
    media_panel_->SetEntries(std::move(entries));
}

void VeApp::WireMediaPanel() {
    if (media_panel_ == nullptr) return;
    EnsureMediaStack();

    MediaPanel::Hooks hooks;
    hooks.on_import = [this] { ImportMediaInteractive(); };
    // URL import lands with the AI-tools wave — surface the honest
    // state instead of a silent no-op.
    hooks.on_import_url = [this] {
        SetStatus("Import from URL — deferred to the tools wave");
    };
    hooks.on_open_source = [this](const std::string& path) {
        const CatalogEntry* e = FindCatalogEntry(path);
        if (e == nullptr) {
            SetStatus("Media not in catalog", true);
            return;
        }
        int fps_num = 30, fps_den = 1;
        if (e->fps > 0.0) {
            fps_num = static_cast<int>(e->fps * 1000.0 + 0.5);
            fps_den = 1000;
        }
        std::string name = e->path;
        const std::size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) name = name.substr(pos + 1);
        OpenInSourceMonitor(e->path, name, e->duration_seconds,
                            fps_num, fps_den, e->HasVideo(),
                            e->has_audio, e->width, e->height);
    };
    hooks.on_add_to_timeline = [this](const std::string& path) {
        AddMediaToTimeline(path);
    };
    hooks.on_remove_entries =
        [this](const std::vector<std::string>& paths) {
            int removed = 0;
            for (const std::string& path : paths) {
                CatalogEntry e;
                if (catalog_ != nullptr && catalog_->IsOpen() &&
                    catalog_->FindByPath(path, &e)) {
                    if (catalog_->Remove(e.id)) {
                        ++removed;
                        // PurgePath drops cached posters/peaks AND
                        // bumps the path's generation so in-flight
                        // decodes can't republish stale results.
                        if (media_prep_ != nullptr) {
                            media_prep_->PurgePath(e.path);
                        }
                    }
                }
            }
            RefreshBinEntries();
            SetStatus(removed > 0
                          ? "Removed " + std::to_string(removed) +
                                " item(s) from the bin"
                          : "Nothing removed",
                      removed == 0);
        };
    hooks.on_reveal_entry = [this](const std::string& path) {
        (void)this;  // capture retained for the non-Windows branch
#if defined(_WIN32)
        // explorer /select,<path> — the same reveal the legacy bin ran.
        const std::wstring wide =
            XER::Helper::Path::FromUtf8(path).wstring();
        std::wstring cmd = L"/select,\"" + wide + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                        cmd.c_str(), nullptr, SW_SHOWNORMAL);
#else
        (void)path;
        SetStatus("Reveal in Explorer is Windows-only", true);
#endif
    };
    hooks.on_create_proxy = [this](const std::string& path) {
        EnsureMediaStack();
        if (proxies_ == nullptr) {
            SetStatus("Proxy manager unavailable", true);
            return;
        }
        ProxySettings settings;
        if (proxies_->Request(path, settings)) {
            SetStatus("Proxy queued");
        } else {
            SetStatus("Proxy already queued or not applicable");
        }
        RefreshBinEntries();   // proxy_state -> pending badge
    };
    hooks.on_cancel_proxy = [this](const std::string& path) {
        if (proxies_ != nullptr) proxies_->Cancel(path);
        RefreshBinEntries();
    };
    hooks.on_select = [](const std::vector<std::string>&) {
        // Bin selection is bin-local for wave-1 (the inspector reads
        // the TIMELINE selection) — nothing to fan out yet.
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    hooks.get_poster =
        [this](const std::string& path, double seconds,
               video::Frame* frame) -> MediaLookup {
        EnsureMediaStack();
        if (media_prep_ == nullptr) return MediaLookup::kFailed;
        switch (media_prep_->LookupPoster(path, seconds, frame)) {
            case MediaPrep::Lookup::kReady: return MediaLookup::kReady;
            case MediaPrep::Lookup::kPending:
                return MediaLookup::kPending;
            default: return MediaLookup::kFailed;
        }
    };
    hooks.get_audio_peaks =
        [this](const std::string& path,
               AudioPeaks* out) -> MediaLookup {
        EnsureMediaStack();
        if (media_prep_ == nullptr) return MediaLookup::kFailed;
        switch (media_prep_->LookupPeaks(path, out)) {
            case MediaPrep::Lookup::kReady: return MediaLookup::kReady;
            case MediaPrep::Lookup::kPending:
                return MediaLookup::kPending;
            default: return MediaLookup::kFailed;
        }
    };
    // Drag-out — the shell owns the session (drag_session.h); the
    // timeline surface gets hover feedback and the drop resolves to
    // a history-backed insert per dragged path.
    hooks.on_begin_drag =
        [this](const std::vector<std::string>& paths,
               XER::UI::Layout::Point) { drag_session_.Begin(paths); };
    hooks.on_drag_update = [this](XER::UI::Layout::Point point) {
        const auto& surface =
            timeline_ != nullptr ? timeline_->Surface() : nullptr;
        drag_session_.Update(point, surface.get());
    };
    hooks.on_end_drag = [this](XER::UI::Layout::Point point) {
        const auto& surface =
            timeline_ != nullptr ? timeline_->Surface() : nullptr;
        auto drop = drag_session_.End(point, surface.get());
        if (drop.has_value()) {
            for (const std::string& path : drag_session_.Paths()) {
                AddMediaToTimelineAt(path, drop->time, drop->track);
            }
        }
    };
    media_panel_->SetHooks(std::move(hooks));
    RefreshBinEntries();
}

void VeApp::ImportMediaFiles(const std::vector<std::string>& paths) {
    EnsureMediaStack();
    if (catalog_ == nullptr || !catalog_->IsOpen()) {
        SetStatus("Media catalog unavailable", true);
        return;
    }
    int imported = 0;
    std::string last_error;
    for (const std::string& path : paths) {
        std::int64_t id = 0;
        std::string error;
        // extract_poster=false — the poster PNG lands on the
        // media-prep worker; ConsumeCatalogDirty refreshes the bin
        // once poster_path is written (legacy ImportMediaFile parity).
        if (catalog_->Import(path, &id, false, &error)) {
            ++imported;
            if (media_prep_ != nullptr) {
                media_prep_->RequestImportPoster(path);
            }
        } else {
            last_error = error;
        }
    }
    RefreshBinEntries();
    if (paths.size() == 1 && imported == 1) {
        SetStatus("Imported " + paths.front());
        return;
    }
    std::string msg = "Imported " + std::to_string(imported) + " of " +
                      std::to_string(paths.size());
    if (imported == 0 && !last_error.empty()) {
        msg += " — " + last_error;
    }
    SetStatus(msg, imported == 0);
}

void VeApp::ImportMediaInteractive() {
#if defined(_WIN32)
    // Minimal GetOpenFileNameW multi-select picker — the legacy
    // IFileOpenDialog path ported to the shell (no parent_hwnd_ seam
    // here; nullptr owner is acceptable for the bin's import).
    wchar_t buffer[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrFilter =
        L"Media\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.mp3;*.wav;"
        L"*.flac;*.aac;*.ogg;*.m4a;*.png;*.jpg;*.jpeg;*.bmp;*.gif;"
        L"*.tif;*.tiff;*.webp;*.exr;*.dpx\0All files\0*.*\0";
    ofn.lpstrTitle = L"Import media";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |
                OFN_EXPLORER | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&ofn)) return;
    std::vector<std::string> paths;
    std::wstring dir = buffer;
    const wchar_t* p = buffer + dir.size() + 1;
    if (*p == L'\0') {
        paths.push_back(
            XER::Helper::Path::NormalizeUtf8Path(
                XER::Helper::String::WideToUtf8(dir)));
    } else {
        while (*p != L'\0') {
            const std::wstring file = p;
            const std::filesystem::path full =
                std::filesystem::path(dir) / file;
            paths.push_back(
                XER::Helper::Path::NormalizeUtf8Path(
                    XER::Helper::String::WideToUtf8(full.wstring())));
            p += file.size() + 1;
        }
    }
    ImportMediaFiles(paths);
#else
    SetStatus("Import dialog is Windows-only", true);
#endif
}

// ---- bin -> timeline ----------------------------------------------------

void VeApp::AddMediaToTimelineAt(const std::string& path,
                                 double at_seconds,
                                 int preferred_track) {
    // Legacy AddMediaToTimelineAt parity (ui/video_editor_app.cc):
    // kind-matched placement, linked A/V pairs, macro + rollback.
    const CatalogEntry* entry = FindCatalogEntry(path);
    if (entry == nullptr) {
        SetStatus("Select media in the bin first", true);
        return;
    }
    if (history_ == nullptr) {
        SetStatus("Timeline not ready", true);
        return;
    }
    const TrackKind wanted =
        entry->HasVideo() ? TrackKind::Video : TrackKind::Audio;
    const bool linked_av = entry->HasVideo() && entry->HasAudio();
    const auto first_unlocked = [this](TrackKind kind) {
        for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
            if (project_.tracks[i].kind == kind &&
                !project_.tracks[i].locked) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    history_->BeginMacro("Add clip");
    bool applied_any = false;
    bool ok = true;
    std::string error;
    std::string clip_id;
    std::string link_group;

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
        track_index = history_->AddTrack(
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
            audio_track = history_->AddTrack(TrackKind::Audio, "Audio");
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
        if (timeline_ != nullptr &&
            timeline_->Surface() != nullptr &&
            timeline_->Surface()->SnapEnabled()) {
            clip_start = SnapTime(project_, track_index, clip_start,
                                  0.1, playhead_);
        }
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
                entry->width, entry->height, project_.width,
                project_.height);
        }
        if (linked_av) {
            link_group = NewClipId();
            clip.link_group = link_group;
        }
        MediaBounds bounds;
        bounds.duration_seconds = entry->duration_seconds;
        bounds.has_video = entry->HasVideo();
        ok = history_->AddClip(std::move(clip), bounds, &clip_id,
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
        MediaBounds audio_bounds;
        audio_bounds.duration_seconds = entry->duration_seconds;
        std::string audio_id;
        ok = history_->AddClip(std::move(audio_clip), audio_bounds,
                               &audio_id, &error);
        applied_any |= ok;
    }
    history_->EndMacro();
    if (!ok) {
        if (applied_any) {
            history_->Undo();   // rolls the whole macro back
        }
        SetStatus("Add clip failed: " + error, true);
        return;
    }
    if (bridge_ != nullptr) {
        bridge_->SelectClip(clip_id);
    }
    NotifyTimelineChanged();
    SetStatus("Added " + path.substr(path.find_last_of("/\\") + 1));
}

// ---- selection provider / inspector commit ------------------------------

std::vector<Clip> VeApp::SelectedClips() const {
    std::vector<Clip> out;
    if (bridge_ == nullptr) return out;
    const std::string& id = bridge_->SelectedClipId();
    if (id.empty()) return out;
    if (const Clip* clip = FindClip(project_, id)) {
        out.push_back(*clip);
    }
    return out;
}

void VeApp::RefreshInspector() {
    if (inspector_panel_ != nullptr) {
        inspector_panel_->RefreshSelection();
    }
}

void VeApp::CommitInspectorClips(const std::vector<Clip>& clips) {
    if (history_ == nullptr || clips.empty()) return;
    const bool macro = clips.size() > 1;
    if (macro) history_->BeginMacro("Inspector edit");
    bool any = false;
    for (const Clip& clip : clips) {
        MediaBounds bounds;
        if (const CatalogEntry* e = FindCatalogEntry(clip.source_path)) {
            bounds.duration_seconds = e->duration_seconds;
            bounds.has_video = e->HasVideo();
        } else {
            bounds.duration_seconds = clip.duration;
            bounds.has_video = true;
        }
        std::string error;
        any |= history_->ReplaceClip(clip.id, clip, bounds, &error);
        if (!error.empty()) {
            SetStatus("Inspector edit rejected: " + error, true);
            break;
        }
    }
    if (macro) history_->EndMacro();
    if (any) NotifyTimelineChanged();
}

// ---- effects metadata + apply ------------------------------------------

std::string VeApp::FxDisplayName(const std::string& fx_type) const {
    for (const FxEntry& e : fx_entries_) {
        if (e.type == fx_type) return e.name;
    }
    // Types can arrive from project files before the catalog built —
    // resolve live against the plugin host too.
    if (const PluginDesc* desc =
            PluginHost::ForEffectType(fx_type)) {
        return desc->name;
    }
    return fx_type;
}

std::vector<InspectorParamSpec> VeApp::FxParamSpecs(
    const std::string& fx_type) const {
    std::vector<InspectorParamSpec> specs;
    if (const PluginDesc* desc =
            PluginHost::ForEffectType(fx_type)) {
        for (const XER_ParamDesc& p : desc->params) {
            InspectorParamSpec spec;
            spec.name = p.name != nullptr ? p.name : "param";
            spec.minimum = p.min_value;
            spec.maximum = p.max_value;
            spec.step = (p.max_value - p.min_value) / 100.0f;
            if (spec.step <= 0.0f) spec.step = 0.01f;
            spec.default_value = p.default_value;
            specs.push_back(spec);
        }
        return specs;
    }
    // Core/script types — FxEntry defaults carry the count; names
    // fall back to "param i" in the panel.
    for (const FxEntry& e : fx_entries_) {
        if (e.type == fx_type) {
            for (std::size_t i = 0; i < e.default_params.size(); ++i) {
                InspectorParamSpec spec;
                spec.name = "param " + std::to_string(i);
                spec.default_value = e.default_params[i];
                specs.push_back(spec);
            }
            return specs;
        }
    }
    return specs;
}

void VeApp::ApplyFxToClips(const std::vector<std::string>& clip_ids,
                           const std::string& fx_type) {
    if (history_ == nullptr) return;
    if (clip_ids.empty() || fx_type.empty()) {
        SetStatus("Select a clip before applying an effect", true);
        return;
    }
    std::vector<float> params;
    for (const FxEntry& e : fx_entries_) {
        if (e.type == fx_type) {
            params = e.default_params;
            break;
        }
    }
    history_->BeginMacro("Apply FX");
    int applied = 0;
    for (const std::string& id : clip_ids) {
        const Clip* clip = FindClip(project_, id);
        if (clip == nullptr) continue;
        Clip updated = *clip;
        updated.effects.push_back({fx_type, params});
        MediaBounds bounds;
        if (const CatalogEntry* e =
                FindCatalogEntry(clip->source_path)) {
            bounds.duration_seconds = e->duration_seconds;
            bounds.has_video = e->HasVideo();
        } else {
            bounds.duration_seconds = clip->duration;
        }
        std::string error;
        if (history_->ReplaceClip(id, updated, bounds, &error)) {
            ++applied;
        } else {
            SetStatus("Apply FX failed: " + error, true);
            break;
        }
    }
    history_->EndMacro();
    if (applied > 0) {
        NotifyTimelineChanged();
        SetStatus("Applied " + FxDisplayName(fx_type) + " to " +
                  std::to_string(applied) + " clip(s)");
    }
}

void VeApp::WireEffectsPanel() {
    if (effects_panel_ == nullptr) return;
    fx_entries_ = BuildFxRegistryEntries();
    effects_panel_->SetEntries(fx_entries_);
    effects_panel_->SetClipSelectionProvider([this] {
        std::vector<std::string> ids;
        if (bridge_ != nullptr && !bridge_->SelectedClipId().empty()) {
            ids.push_back(bridge_->SelectedClipId());
        }
        return ids;
    });
    effects_panel_->SetOnApplyFx(
        [this](const std::vector<std::string>& clip_ids,
               const std::string& fx_type) {
            ApplyFxToClips(clip_ids, fx_type);
        });
    effects_panel_->SetOnStatus(
        [this](const std::string& s, bool err) { SetStatus(s, err); });
}

void VeApp::WireInspectorPanel() {
    if (inspector_panel_ == nullptr) return;
    inspector_panel_->SetSelectionProvider(
        [this] { return SelectedClips(); });

    InspectorHooks hooks;
    hooks.on_commit_clips =
        [this](const std::vector<Clip>& clips) {
            CommitInspectorClips(clips);
        };
    hooks.on_begin_macro = [this] {
        if (history_ != nullptr) {
            history_->BeginMacro("Inspector gesture");
        }
    };
    hooks.on_end_macro = [this] {
        if (history_ != nullptr) history_->EndMacro();
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    hooks.fx_display_name = [this](const std::string& type) {
        return FxDisplayName(type);
    };
    hooks.fx_param_specs = [this](const std::string& type) {
        return FxParamSpecs(type);
    };
    hooks.fx_catalog = [this] {
        // Video/track effects only — audio fx reach clips through a
        // different chain (bounce/VST3), not clip.effects.
        std::vector<std::pair<std::string, std::string>> out;
        for (const FxEntry& e : fx_entries_) {
            if (e.badge != FxEntry::Badge::Audio) {
                out.emplace_back(e.type, e.name);
            }
        }
        return out;
    };
    hooks.voice_isolate_available = [this](const Clip& clip) {
        // Shown when the clip's media carries audio AND the GTCRN
        // model is staged (voice_isolate.h — Available() reports the
        // staged-model + ONNX-runtime verdict).
        const CatalogEntry* e = FindCatalogEntry(clip.source_path);
        return e != nullptr && e->has_audio &&
               VoiceIsolate::Available();
    };
    hooks.on_apply_voice_isolate =
        [this](const std::vector<std::string>& ids) {
            // The live chain is VST3/script-only; builtin af kernels
            // reach media through the offline bounce (legacy
            // BounceAudioFx). Honest deferred state for wave-1.
            (void)ids;
            SetStatus("Voice isolate — offline bounce lands with the "
                      "audio tools wave");
        };
    inspector_panel_->SetHooks(std::move(hooks));
    inspector_panel_->RefreshSelection();
}

// ---- M3 wave-2 panels: Color / Scopes / Mix ------------------------------

void VeApp::WireColorPanel() {
    if (color_panel_ == nullptr) return;
    // Selection seam — InspectorPanel parity (single-clip surface).
    color_panel_->SetClipProvider([this] {
        std::vector<Clip> sel = SelectedClips();
        return sel.empty() ? Clip{} : sel.front();
    });

    ColorHooks hooks;
    hooks.on_color_changed =
        [this](const std::string& clip_id,
               const ColorParams& params) {
            CommitClipColor(clip_id, params);
        };
    hooks.on_begin_macro = [this] {
        if (history_ != nullptr) {
            history_->BeginMacro("Color gesture");
        }
    };
    hooks.on_end_macro = [this] {
        if (history_ != nullptr) history_->EndMacro();
    };
    // White-balance eyedropper: arming turns the program monitor's
    // click-point seam on — the armed click lands in
    // WireMonitorPanels' on_click_point, which samples the current
    // frame and feeds it back through ApplyPickedColor.
    hooks.on_arm_pick = [this] {
        color_pick_armed_ = true;
        if (program_monitor_ != nullptr) {
            program_monitor_->SetClickPointsArmed(true);
        }
    };
    // Scope chips -> activate the Scopes dock tab (index 7 in the
    // spec §5 order) + set its mode.
    hooks.on_scope_mode = [this](int index) {
        if (scopes_panel_ != nullptr && index >= 0 && index < 4) {
            scopes_panel_->SetMode(
                static_cast<ScopesPanel::Mode>(index));
        }
        if (right_dock_ != nullptr) {
            right_dock_->SetActiveIndex(kRightTabScopes);
        }
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    color_panel_->SetHooks(std::move(hooks));
    color_panel_->RefreshClip();
}

void VeApp::CommitClipColor(const std::string& clip_id,
                            const ColorParams& params) {
    if (history_ == nullptr) return;
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Color commit: clip not found", true);
        return;
    }
    Clip updated = *clip;

    // Upsert helper: writes the param snapshot into an existing slot
    // or appends the effect when it carries a non-default value (a
    // neutral write to a missing effect stays absent so untouched
    // clips keep an empty fx rack).
    const auto upsert = [&updated](const char* type,
                                   std::vector<float> values,
                                   bool non_default) {
        for (ClipEffect& fx : updated.effects) {
            if (fx.type == type) {
                fx.params = std::move(values);
                return;
            }
        }
        if (non_default) {
            updated.effects.push_back({type, std::move(values)});
        }
    };

    // temperature / tint — axes shared by the OKLCH picker cast and
    // the eyedropper correction (color_panel.cc mapping).
    upsert("builtin:xer.fx.temperature", {params.temperature},
           params.temperature != 0.0f);
    upsert("builtin:xer.fx.tint", {params.tint},
           params.tint != 0.0f);

    // Wheel LEVELS -> lift_gamma_gain {lift, gamma, gain}:
    //   shadows.level -> lift   (-1..1 direct)
    //   midtones.level -> gamma (2^level: +1 -> 2.0 brightens mids)
    //   highlights.level -> gain (1 + level -> 0..2)
    const auto& w = params.wheels;
    const bool levels_non_default =
        w[0].level != 0.0f || w[1].level != 0.0f ||
        w[2].level != 0.0f;
    upsert("builtin:xer.fx.lift_gamma_gain",
           {std::clamp(w[0].level, -1.0f, 1.0f),
            std::clamp(std::pow(2.0f, w[1].level), 0.2f, 4.0f),
            std::clamp(1.0f + w[2].level, 0.0f, 4.0f)},
           levels_non_default);

    // Wheel OFFSETS -> channel_mixer diagonal gains. The kernel set
    // has no band-local chroma op, so each wheel's offset folds to a
    // global per-channel delta weighted by band (shadows .5, mids
    // 1.0, highlights .5 — mids dominate, documented approximation).
    // +x pushes R+/B- (warm/cool), +y pushes G-/R+B+ (magenta/green).
    constexpr float kBandW[3] = {0.5f, 1.0f, 0.5f};
    constexpr float kOffsetGain = 0.5f;   // full deflection -> ±0.5
    float dr = 0.0f, dg = 0.0f, db = 0.0f;
    bool offsets_non_default = false;
    for (int i = 0; i < 3; ++i) {
        const float ox = w[i].offset_x * kBandW[i] * kOffsetGain;
        const float oy = w[i].offset_y * kBandW[i] * kOffsetGain;
        if (w[i].offset_x != 0.0f || w[i].offset_y != 0.0f) {
            offsets_non_default = true;
        }
        dr += ox + oy;
        dg += -2.0f * oy;
        db += -ox + oy;
    }
    upsert("builtin:xer.fx.channel_mixer",
           {std::clamp(1.0f + dr, 0.0f, 2.0f),
            std::clamp(1.0f + dg, 0.0f, 2.0f),
            std::clamp(1.0f + db, 0.0f, 2.0f)},
           offsets_non_default);

    MediaBounds bounds;
    if (const CatalogEntry* e = FindCatalogEntry(clip->source_path)) {
        bounds.duration_seconds = e->duration_seconds;
        bounds.has_video = e->HasVideo();
    } else {
        bounds.duration_seconds = clip->duration;
    }
    std::string error;
    if (!history_->ReplaceClip(clip_id, updated, bounds, &error)) {
        SetStatus("Color commit failed: " + error, true);
        return;
    }
    // Continuous drags coalesce inside ReplaceClip; a fresh paint
    // pass for the monitor lands with the next DrainPending.
    NotifyTimelineChanged();
}

XER::UI::Theme::Color VeApp::SampleProgramFrame(float nx,
                                                float ny) const {
    // 5x5 mean around the frame-normalized point — the same region
    // average the eyedropper widget's sample-radius knob applies.
    const auto frame = program_monitor_ != nullptr
                           ? program_monitor_->VideoSurface()
                                 .CurrentFrame()
                           : MonitorSurface::Frame{};
    if (frame.bgra == nullptr || frame.width <= 0 ||
        frame.height <= 0) {
        return {0.5f, 0.5f, 0.5f, 1.0f};
    }
    const int cx = std::clamp(static_cast<int>(nx * frame.width), 0,
                              frame.width - 1);
    const int cy = std::clamp(static_cast<int>(ny * frame.height),
                              0, frame.height - 1);
    double r = 0.0, g = 0.0, b = 0.0;
    int n = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= frame.width ||
                y >= frame.height) {
                continue;
            }
            const auto* px =
                frame.bgra->data() +
                (static_cast<std::size_t>(y) * frame.width + x) * 4;
            b += px[0];
            g += px[1];
            r += px[2];
            ++n;
        }
    }
    if (n == 0) return {0.5f, 0.5f, 0.5f, 1.0f};
    const float inv = 1.0f / (255.0f * n);
    return {static_cast<float>(r * inv),
            static_cast<float>(g * inv),
            static_cast<float>(b * inv), 1.0f};
}

void VeApp::WireScopesPanel() {
    if (scopes_panel_ == nullptr) return;
    // Pull seam — the program monitor's last pushed frame (shared,
    // no copy). Pull-provider feeds aren't retained by the surface,
    // so scopes see the push path only (documented on
    // MonitorSurface::CurrentFrame).
    scopes_panel_->SetFrameSource([this]() -> ScopeFrame {
        if (program_monitor_ == nullptr) return {};
        const auto f =
            program_monitor_->VideoSurface().CurrentFrame();
        return {f.bgra, f.width, f.height};
    });
}

void VeApp::WireMixerPanel() {
    if (mixer_panel_ == nullptr) return;
    mixer_panel_->SetDocument(&project_);

    MixerHooks hooks;
    const auto commit = [this](bool ok, const char* what,
                               const std::string& error) {
        if (!ok) {
            SetStatus(std::string(what) + " failed: " + error,
                      true);
        }
    };
    hooks.on_track_gain = [this, commit](int track, float db) {
        std::string error;
        const bool ok = history_ != nullptr &&
                        history_->SetTrackGainDb(track, db, &error);
        commit(ok, "Track gain", error);
        if (ok) NotifyTimelineChanged();
    };
    hooks.on_track_pan = [this, commit](int track, float pan) {
        std::string error;
        const bool ok = history_ != nullptr &&
                        history_->SetTrackPan(track, pan, &error);
        commit(ok, "Track pan", error);
        if (ok) NotifyTimelineChanged();
    };
    hooks.on_track_mute = [this, commit](int track, bool muted) {
        std::string error;
        const bool ok = history_ != nullptr &&
                        history_->SetTrackMuted(track, muted, &error);
        commit(ok, "Track mute", error);
        if (ok) NotifyTimelineChanged();
    };
    hooks.on_track_solo = [this](int, bool) {
        // Track has no solo flag — honest deferred state (the S
        // button stays toggleable for the M3+ model addition).
        SetStatus("Solo isn't wired into the track model yet");
    };
    hooks.on_master_gain = [this, commit](float db) {
        std::string error;
        const bool ok = history_ != nullptr &&
                        history_->SetMasterGainDb(db, &error);
        commit(ok, "Master gain", error);
        if (ok) {
            // Live preview mirror — the binding forwards to the
            // engine's audio gain outside history (old app parity).
            program_engine_.SetMasterGainDb(db);
            NotifyTimelineChanged();
        }
    };
    hooks.on_begin_macro = [this] {
        if (history_ != nullptr) {
            history_->BeginMacro("Mixer gesture");
        }
    };
    hooks.on_end_macro = [this] {
        if (history_ != nullptr) history_->EndMacro();
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    mixer_panel_->SetHooks(std::move(hooks));

    // Levels seam — pull-based per strip. Master reads the program
    // binding's last stereo peaks; per-track taps don't exist yet
    // (PreviewEngine mixes to the stereo bus), so track strips get
    // an honest zero until a per-track peak seam lands.
    mixer_panel_->SetLevelsProvider([this](int track_index) {
        if (track_index == MixerPanel::kMasterBus) {
            return program_engine_.PeakLevels();
        }
        return std::pair<float, float>{0.0f, 0.0f};
    });
}

// ---- M3 wave-3 panels: Markers / History / Queue / Ext / Transcript ---
// Same ownership rules as wave-1/2: the panels hold presentation
// snapshots only; every document mutation funnels through history_
// (marker ops) or the engine seams (export, ExtHost, whisper worker).

void VeApp::WireMarkersPanel() {
    if (markers_panel_ == nullptr) return;
    MarkersPanel::Hooks hooks;
    hooks.on_seek = [this](double t) { Seek(t); };
    hooks.playhead_seconds = [this] { return playhead_; };
    hooks.format_timecode = [this](double t) {
        return FormatTimecode(t, project_.fps_num, project_.fps_den);
    };
    hooks.on_add_marker = [this](const Marker& marker) {
        if (history_ == nullptr) return;
        std::string error;
        if (!history_->AddMarker(marker, &error)) {
            SetStatus("Add marker failed: " + error, true);
            return;
        }
        NotifyTimelineChanged();
        SetStatus("Marker added");
    };
    hooks.on_remove_marker = [this](double time) {
        if (history_ != nullptr && history_->RemoveMarker(time)) {
            NotifyTimelineChanged();
            SetStatus("Marker removed");
        }
    };
    // Rename/recolor is remove+add — one history macro so undo
    // restores both halves (the panel's on_update_marker contract).
    hooks.on_update_marker =
        [this](double old_time, const Marker& updated) {
            if (history_ == nullptr) return;
            history_->BeginMacro("Edit marker");
            history_->RemoveMarker(old_time);
            std::string error;
            const bool ok = history_->AddMarker(updated, &error);
            history_->EndMacro();
            if (!ok) {
                SetStatus("Edit marker failed: " + error, true);
                history_->Undo();   // restore the removed marker
            }
            NotifyTimelineChanged();
        };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    markers_panel_->SetHooks(std::move(hooks));
    RefreshMarkersPanel();
}

void VeApp::RefreshMarkersPanel() {
    if (markers_panel_ == nullptr) return;
    // Project markers sorted by time. The model has no clip-anchored
    // markers yet — MarkerRow.scope exists for that feed and stays
    // "Project" today (markers_panel.h seam doc).
    std::vector<MarkerRow> rows;
    rows.reserve(project_.markers.size());
    for (const Marker& m : project_.markers) {
        MarkerRow row;
        row.marker = m;
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(),
              [](const MarkerRow& a, const MarkerRow& b) {
                  return a.marker.time < b.marker.time;
              });
    markers_panel_->SetMarkers(std::move(rows));
}

void VeApp::WireHistoryPanel() {
    if (history_panel_ == nullptr) return;
    HistoryPanel::Hooks hooks;
    // No jump-to-depth op exists on TimelineHistory — a click is N
    // repeated Undo()/Redo() calls (history_panel.h contract).
    hooks.on_undo_steps = [this](int steps) {
        int done = 0;
        for (; done < steps; ++done) {
            if (history_ == nullptr || !history_->Undo()) break;
        }
        if (done > 0) {
            NotifyTimelineChanged();
            SetStatus("Undo " + std::to_string(done) +
                      (done == 1 ? " step" : " steps"));
        }
    };
    hooks.on_redo_steps = [this](int steps) {
        int done = 0;
        for (; done < steps; ++done) {
            if (history_ == nullptr || !history_->Redo()) break;
        }
        if (done > 0) {
            NotifyTimelineChanged();
            SetStatus("Redo " + std::to_string(done) +
                      (done == 1 ? " step" : " steps"));
        }
    };
    hooks.on_clear = [this] {
        if (history_ != nullptr) {
            history_->Clear();
            NotifyTimelineChanged();
            SetStatus("History cleared");
        }
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    history_panel_->SetHooks(std::move(hooks));
    RefreshHistoryPanel();
}

void VeApp::RefreshHistoryPanel() {
    if (history_panel_ == nullptr || history_ == nullptr) return;
    history_panel_->SetEntries(history_->UndoLabels(),
                               history_->RedoLabels());
}

void VeApp::WireQueuePanel() {
    if (queue_panel_ == nullptr) return;
    QueuePanel::Hooks hooks;
    // "+ Enqueue" composes a job for the current sequence. Output
    // lands under the OS temp dir (a save-as picker lands with the
    // export-settings wave — the path is surfaced on the row).
    hooks.on_enqueue = [this] {
        const int serial = ++export_serial_;
        std::string base = "sequence";
        if (!project_path_.empty()) {
            base = project_path_;
            const std::size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) base = base.substr(slash + 1);
            const std::size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
        }
        QueueJob job;
        job.id = "export-" + std::to_string(serial);
        job.name = base + ".mp4";
        job.format = "H.264 MP4 · 8 Mbps";
        job.detail =
            project_.work_out > project_.work_in
                ? "work area " +
                      FormatTimecode(project_.work_in, project_.fps_num,
                                     project_.fps_den) +
                      "–" +
                      FormatTimecode(project_.work_out,
                                     project_.fps_num, project_.fps_den)
                : "full sequence";
        job.output_path =
            (std::filesystem::temp_directory_path() / "xer_exports" /
             (job.id + ".mp4"))
                .string();
        queue_panel_->Enqueue(std::move(job));
    };
    hooks.on_submit = [this](const std::string& id) {
        StartQueuedExport(id);
    };
    hooks.on_cancel = [this](const std::string& id) {
        if (id == export_active_id_ && export_engine_ != nullptr) {
            export_engine_->Cancel();   // cooperative — status lands via pump
        }
        if (id == export_pending_id_) export_pending_id_.clear();
    };
    // Honest unsupported seams — ExportEngine has no pause/resume
    // (queue_panel.h "engine seam" doc). Returning false lets the
    // panel say so instead of faking a frozen row.
    hooks.on_pause = [](const std::string&) { return false; };
    hooks.on_resume = [](const std::string&) { return false; };
    hooks.on_retry = [](const std::string&) {
        // The panel itself requeues the row; on_submit re-fires when
        // it reaches the head — nothing for the shell to store.
    };
    hooks.on_reveal = [this](const std::string& id) {
        const QueueJob* job = queue_panel_->JobById(id);
        if (job == nullptr || job->output_path.empty()) return;
        (void)this;
#if defined(_WIN32)
        const std::wstring wide =
            XER::Helper::Path::FromUtf8(job->output_path).wstring();
        std::wstring cmd = L"/select,\"" + wide + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                        cmd.c_str(), nullptr, SW_SHOWNORMAL);
#else
        SetStatus("Reveal in Explorer is Windows-only", true);
#endif
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    queue_panel_->SetHooks(std::move(hooks));
}

void VeApp::StartQueuedExport(const std::string& job_id) {
    if (queue_panel_ == nullptr) return;
    if (export_engine_ != nullptr && export_engine_->IsRunning()) {
        // Single worker: the cancelled head is still draining — hold
        // the row queued and retry when PumpExportQueue frees the
        // engine (avoids an immediate "already running" rejection).
        queue_panel_->WithdrawSubmission(job_id);
        export_pending_id_ = job_id;
        return;
    }
    const QueueJob* job = queue_panel_->JobById(job_id);
    if (job == nullptr) return;
    ExportOptions options;
    options.output_path = job->output_path;
    if (options.output_path.empty()) {
        queue_panel_->SetJobState(job_id, QueueJobState::Failed,
                                  "no output path");
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(options.output_path).parent_path(), ec);
    if (export_engine_ == nullptr) {
        export_engine_ =
            std::make_unique<XER::VideoEditor::ExportEngine>();
    }
    std::string error;
    // No progress callback — Tick polls Progress()/Status() so the
    // worker thread never touches widgets.
    if (!export_engine_->Start(project_, options, {}, &error)) {
        queue_panel_->SetJobState(job_id, QueueJobState::Failed,
                                  error.empty() ? "export start failed"
                                                : error);
        SetStatus("Export failed: " + error, true);
        return;
    }
    export_active_id_ = job_id;
    export_elapsed_ = 0.0;
    SetStatus("Exporting " + job->name);
}

void VeApp::PumpExportQueue(double dt_seconds) {
    if (queue_panel_ == nullptr) return;
    if (export_engine_ != nullptr && !export_active_id_.empty()) {
        export_elapsed_ += dt_seconds;
        if (export_engine_->IsRunning()) {
            const ExportProgress p = export_engine_->Progress();
            double eta = -1.0;
            if (p.fraction > 0.01 && export_elapsed_ > 0.0) {
                eta = export_elapsed_ * (1.0 - p.fraction) /
                      p.fraction;
            }
            queue_panel_->SetJobProgress(export_active_id_,
                                         p.fraction, eta);
        } else {
            const std::string id = export_active_id_;
            export_active_id_.clear();
            switch (export_engine_->Status()) {
                case ExportStatus::kDone:
                    queue_panel_->SetJobState(id, QueueJobState::Done);
                    SetStatus("Export finished");
                    break;
                case ExportStatus::kCancelled:
                    queue_panel_->SetJobState(id,
                                              QueueJobState::Cancelled);
                    SetStatus("Export cancelled");
                    break;
                case ExportStatus::kError:
                    queue_panel_->SetJobState(
                        id, QueueJobState::Failed,
                        export_engine_->Error());
                    SetStatus("Export failed: " +
                                  export_engine_->Error(),
                              true);
                    break;
                default:
                    queue_panel_->SetJobState(
                        id, QueueJobState::Cancelled, "engine stopped");
                    break;
            }
            // Reap the finished worker now (non-blocking join).
            export_engine_->Wait();
        }
    }
    // A job parked while the engine drained gets resubmitted once the
    // worker frees (fresh engine — Start() on a finished instance is
    // legal, but a clean one keeps the state obvious).
    if (!export_pending_id_.empty() &&
        (export_engine_ == nullptr || !export_engine_->IsRunning())) {
        const std::string id = export_pending_id_;
        export_pending_id_.clear();
        export_engine_.reset();
        if (const QueueJob* job = queue_panel_->JobById(id);
            job != nullptr) {
            queue_panel_->SetJobState(id, QueueJobState::Running);
            StartQueuedExport(id);
        }
    }
}

std::string VeApp::ExtensionsDir() const {
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) return {};
    return (cfg.assets_dir / "extensions").string();
}

void VeApp::RefreshExtensionsPanel(bool force) {
    if (extensions_panel_ == nullptr) return;
    if (force || !ext_scanned_) {
        ext_scanned_ = true;
        const std::string dir = ExtensionsDir();
        if (!dir.empty()) {
            // Scan() verifies every .xexrj and registers the verified
            // ones with the xer-ext:// scheme (ext_host.h).
            ExtHost::Shared().Scan(dir);
        }
    }
    std::vector<ExtensionRow> rows;
    for (const Extension& e : ExtHost::Shared().Entries()) {
        ExtensionRow row;
        row.id = e.manifest.id;
        row.name = e.manifest.name.empty() ? e.manifest.id
                                           : e.manifest.name;
        row.version = e.manifest.version;
        row.path = e.path;
        row.verified = e.verified;
        row.has_native = e.has_native;
        row.has_ui = !e.manifest.ui.empty();
        row.enabled = !disabled_extensions_.count(e.manifest.id);
        row.status = e.verified
            ? "verified"
            : (e.verify_errors.empty() ? "unverified"
                                       : e.verify_errors.front());
        row.entry = e.manifest.entry;
        row.ui_member = e.manifest.ui;
        row.depends = e.manifest.required;
        row.native_dlls = e.manifest.native_dll;
        row.native_lua = e.manifest.native_lua;
        row.native_py = e.manifest.native_py;
        row.errors = e.verify_errors;
        rows.push_back(std::move(row));
    }
    extensions_panel_->SetExtensions(std::move(rows));
}

void VeApp::WireExtensionsPanel() {
    if (extensions_panel_ == nullptr) return;
    // JS openUi() + the row's UI button funnel to the same handler.
    // ui2 has no CEF web view yet — the honest seam resolves the URL
    // and reports it; hosting the page lands with the CEF wave.
    ExtHost::Shared().SetOpenUiFn([this](const std::string& url) {
        Notify("Extension UI",
               url + " — xer-ext hosting panel is deferred to the "
                     "CEF wave");
    });

    ExtensionsPanel::Hooks hooks;
    hooks.on_set_enabled =
        [this](const std::string& id, bool enabled) {
            // Session-only gating — ExtHost has no persistent
            // enable/disable concept (extensions_panel.h seam doc).
            if (enabled) disabled_extensions_.erase(id);
            else disabled_extensions_.insert(id);
            SetStatus(id + (enabled ? " enabled" : " disabled"));
        };
    hooks.on_run = [this](const std::string& id) {
        const ExtHost::RunResult result = ExtHost::Shared().Run(id);
        if (!result.ok) {
            SetStatus("Run " + id + " failed: " + result.error, true);
            return;
        }
        SetStatus("Ran " + id +
                  (result.output.empty()
                       ? ""
                       : " — " + result.output));
    };
    hooks.on_open_ui = [this](const std::string& id) {
        const ExtensionRow* row = extensions_panel_->ExtensionById(id);
        if (row == nullptr || row->ui_member.empty()) return;
        // Same URL ExtHost's JS openUi() would produce — routed
        // through the installed open_ui_fn so both paths share the
        // handler.
        const std::string url =
            Cef::ExtSchemeUrl(id, row->ui_member);
        Notify("Extension UI",
               url + " — xer-ext hosting panel is deferred to the "
                     "CEF wave");
    };
    hooks.on_reveal = [this](const std::string& path) {
        if (path.empty()) return;
        (void)this;
#if defined(_WIN32)
        const std::wstring wide =
            XER::Helper::Path::FromUtf8(path).wstring();
        std::wstring cmd = L"/select,\"" + wide + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                        cmd.c_str(), nullptr, SW_SHOWNORMAL);
#else
        SetStatus("Reveal in Explorer is Windows-only", true);
#endif
    };
    hooks.on_rescan = [this] { RefreshExtensionsPanel(true); };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    extensions_panel_->SetHooks(std::move(hooks));
    RefreshExtensionsPanel();
}

// ---- transcript subject + whisper job ---------------------------------

void VeApp::BindTranscriptTo(const std::string& media_path,
                             const std::string& display_name) {
    transcript_media_path_ = media_path;
    transcript_media_name_ = display_name;
    transcript_clip_id_.clear();
    if (transcript_panel_ != nullptr) {
        transcript_panel_->SetSubject(media_path, display_name,
                                      nullptr);
    }
    // Cache replay (memory first, then the on-disk JSON cache).
    const auto it = transcript_cache_.find(media_path);
    if (it != transcript_cache_.end()) {
        transcript_panel_->SetSegments(it->second);
    } else if (!media_path.empty()) {
        const auto cache_dir =
            prefs_path_.parent_path() / "transcripts";
        const auto json =
            cache_dir / (TranscriptCacheKey(media_path) + ".json");
        std::vector<Ai::TranscriptSegment> cached;
        std::error_code ec;
        if (std::filesystem::exists(json, ec) &&
            LoadTranscriptCache(json.string(), media_path, &cached)) {
            transcript_cache_[media_path] = cached;
            transcript_panel_->SetSegments(std::move(cached));
        }
    }
}

void VeApp::BindTranscriptToClip(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    transcript_clip_id_ = clip_id;
    transcript_media_path_ = clip->source_path;
    std::string name = clip->source_path;
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    transcript_media_name_ = name;
    if (transcript_panel_ != nullptr) {
        transcript_panel_->SetSubject(clip->source_path, name, clip);
        const auto it = transcript_cache_.find(clip->source_path);
        if (it != transcript_cache_.end()) {
            transcript_panel_->SetSegments(it->second);
        } else {
            // Disk cache replay — same path as BindTranscriptTo.
            const auto cache_dir =
                prefs_path_.parent_path() / "transcripts";
            const auto json =
                cache_dir /
                (TranscriptCacheKey(clip->source_path) + ".json");
            std::vector<Ai::TranscriptSegment> cached;
            std::error_code ec;
            if (!clip->source_path.empty() &&
                std::filesystem::exists(json, ec) &&
                LoadTranscriptCache(json.string(), clip->source_path,
                                    &cached)) {
                transcript_cache_[clip->source_path] = cached;
                transcript_panel_->SetSegments(std::move(cached));
            }
        }
    }
}

void VeApp::RefreshTranscriptSubject() {
    if (transcript_panel_ == nullptr || transcript_clip_id_.empty()) {
        return;
    }
    const Clip* clip = FindClip(project_, transcript_clip_id_);
    if (clip == nullptr) {
        // The bound clip may have been split/ripple-deleted — adopt
        // the first clip still consuming the same media so the
        // source->timeline map stays live.
        for (const Clip& c : project_.clips) {
            if (c.source_path == transcript_media_path_) {
                clip = &c;
                transcript_clip_id_ = c.id;
                break;
            }
        }
    }
    if (clip != nullptr) {
        // Same media path → SetSubject keeps the words and refreshes
        // only the mapping snapshot.
        transcript_panel_->SetSubject(transcript_media_path_,
                                      transcript_media_name_, clip);
    }
}

void VeApp::InjectTranscript(
    const std::vector<Ai::TranscriptSegment>& segments) {
    if (!transcript_media_path_.empty()) {
        transcript_cache_[transcript_media_path_] = segments;
    }
    if (transcript_panel_ != nullptr) {
        transcript_panel_->SetSegments(segments);
    }
}

void VeApp::StartTranscription() {
    if (transcript_panel_ == nullptr) return;
    if (transcript_media_path_.empty()) {
        SetStatus("Bind a media clip to the transcript first", true);
        return;
    }
    if (transcript_running_) {
        SetStatus("Transcription already running");
        return;
    }
    std::string detail;
    if (!Ai::WhisperAvailable(&detail)) {
        SetStatus("Whisper unavailable" +
                      (detail.empty() ? std::string() : " — " + detail),
                  true);
        return;
    }
    const std::string model = Ai::FindWhisperModel();
    if (model.empty()) {
        SetStatus("No whisper model — set XER_WHISPER_MODEL or drop a "
                  "ggml*.bin in models/", true);
        return;
    }
    transcript_cancel_ = false;
    transcript_progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(transcript_result_mutex_);
        transcript_result_ready_ = false;
        transcript_result_ok_ = false;
        transcript_result_.clear();
        transcript_result_error_.clear();
    }
    transcript_job_path_ = transcript_media_path_;
    transcript_running_ = true;
    transcript_panel_->SetJobState(true, 0.0f);
    SetStatus("Transcribing " + transcript_media_name_ + "…");
    const std::string path = transcript_job_path_;
    const double cap = Ai::TranscribeMaxSeconds();
    // Worker contract: decode PCM → whisper, write ONLY the
    // mutex-guarded result slot; the UI thread drains it in
    // PumpTranscriptJob (the old shell's async marshalling parity).
    transcript_worker_ = std::thread(
        [this, path, model, cap] {
            Ai::Pcm pcm;
            std::string error;
            std::vector<Ai::TranscriptSegment> segments;
            bool ok = false;
            if (!transcript_cancel_.load() &&
                Ai::ExtractMonoPcm(path, 16000, cap, &pcm, &error)) {
                ok = Ai::Transcribe(
                    pcm.samples, model,
                    [this](float f) { transcript_progress_ = f; },
                    &segments, &error);
            }
            std::lock_guard<std::mutex> lock(
                transcript_result_mutex_);
            transcript_result_ = std::move(segments);
            transcript_result_error_ = error;
            transcript_result_ok_ = ok && !transcript_cancel_.load();
            transcript_result_ready_ = true;
        });
}

void VeApp::PumpTranscriptJob() {
    if (!transcript_running_.load()) return;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(transcript_result_mutex_);
        ready = transcript_result_ready_;
    }
    if (!ready) {
        if (transcript_panel_ != nullptr) {
            transcript_panel_->SetJobState(true,
                                         transcript_progress_.load());
        }
        return;
    }
    if (transcript_worker_.joinable()) transcript_worker_.join();
    transcript_running_ = false;
    std::vector<Ai::TranscriptSegment> segments;
    std::string error;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(transcript_result_mutex_);
        segments = std::move(transcript_result_);
        error = transcript_result_error_;
        ok = transcript_result_ok_;
        transcript_result_ready_ = false;
    }
    if (transcript_panel_ != nullptr) {
        transcript_panel_->SetJobState(false, 0.0f);
    }
    if (!ok) {
        SetStatus("Transcription failed" +
                      (error.empty() ? std::string() : ": " + error),
                  true);
        return;
    }
    // Store: per-media memory cache + the on-disk JSON cache so a
    // rebind (or restart) skips the whisper job entirely.
    if (!transcript_job_path_.empty()) {
        transcript_cache_[transcript_job_path_] = segments;
        const auto cache_dir =
            prefs_path_.parent_path() / "transcripts";
        std::error_code ec;
        std::filesystem::create_directories(cache_dir, ec);
        const auto json =
            cache_dir /
            (TranscriptCacheKey(transcript_job_path_) + ".json");
        if (!SaveTranscriptCache(json.string(), transcript_job_path_,
                                 segments)) {
            SetStatus("Transcript cache write failed", true);
        }
    }
    if (transcript_panel_ != nullptr &&
        transcript_job_path_ == transcript_media_path_) {
        transcript_panel_->SetSegments(std::move(segments));
    }
    SetStatus("Transcription complete");
}

void VeApp::WireTranscriptPanel() {
    if (transcript_panel_ == nullptr) return;
    TranscriptPanel::Hooks hooks;
    hooks.on_seek = [this](double t) { Seek(t); };
    hooks.on_transcribe = [this] { StartTranscription(); };
    hooks.on_ripple_delete = [this](double t0, double t1) {
        if (history_ == nullptr) return;
        std::string error;
        if (!history_->LiftRange(t0, t1, /*close_gap=*/true, &error)) {
            SetStatus("Ripple delete failed: " + error, true);
            return;
        }
        NotifyTimelineChanged();
        SetStatus("Ripple deleted " +
                  FormatTimecode(t1 - t0, project_.fps_num,
                                 project_.fps_den));
    };
    hooks.on_add_marker =
        [this](double time, const std::string& name) {
            if (history_ == nullptr) return;
            Marker m;
            m.time = time;
            m.name = name;
            std::string error;
            if (!history_->AddMarker(m, &error)) {
                SetStatus("Marker failed: " + error, true);
                return;
            }
            NotifyTimelineChanged();
            SetStatus("Marker added from selection");
        };
    hooks.on_make_captions = [this](double src_t0, double src_t1) {
        if (history_ == nullptr || transcript_panel_ == nullptr) return;
        const Clip* bound = FindClip(project_, transcript_clip_id_);
        if (bound == nullptr) {
            SetStatus("Captions need the bound timeline clip", true);
            return;
        }
        CaptionStyle style;
        style.preset =
            CaptionPresetByName(transcript_caption_preset_);
        // Build against a provisional track, then pick the real
        // target: the first unlocked video track with no overlap
        // (the bound clip's own track collides with it, so it is
        // never a valid caption lane), else a new "Captions" track.
        std::vector<Clip> clips = BuildCaptionClips(
            transcript_panel_->Words(), *bound, src_t0, src_t1,
            /*track_index=*/0, style);
        if (clips.empty()) {
            SetStatus("No caption clips in the selection range",
                      true);
            return;
        }
        const auto overlaps = [this, &clips](int track) {
            for (const Clip& c : clips) {
                for (const Clip& e : project_.clips) {
                    if (e.track != track) continue;
                    if (c.timeline_start <
                            e.timeline_start + e.duration &&
                        e.timeline_start <
                            c.timeline_start + c.duration) {
                        return true;
                    }
                }
            }
            return false;
        };
        int track = -1;
        for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
            if (project_.tracks[i].kind == TrackKind::Video &&
                !project_.tracks[i].locked &&
                !overlaps(static_cast<int>(i))) {
                track = static_cast<int>(i);
                break;
            }
        }
        history_->BeginMacro("Make captions");
        if (track < 0) {
            track = history_->AddTrack(TrackKind::Video, "Captions");
        }
        if (track < 0) {
            history_->EndMacro();
            SetStatus("No free video track for captions", true);
            return;
        }
        int added = 0;
        std::string error;
        for (Clip c : clips) {
            c.track = track;
            MediaBounds bounds;
            bounds.duration_seconds = c.duration;
            bounds.has_video = true;
            std::string new_id;
            if (history_->AddClip(std::move(c), bounds, &new_id,
                                  &error)) {
                ++added;
            } else {
                break;
            }
        }
        history_->EndMacro();
        if (added == 0) {
            SetStatus("Captions failed" +
                          (error.empty() ? std::string()
                                         : ": " + error),
                      true);
            if (history_->CanUndo()) history_->Undo();   // roll the macro
            return;
        }
        NotifyTimelineChanged();
        SetStatus("Created " + std::to_string(added) +
                  " caption clip(s)");
    };
    hooks.on_caption_style = [this](const std::string& name) {
        transcript_caption_preset_ =
            name.empty() ? "classic" : name;
    };
    hooks.transcribe_ready = [] {
        return Ai::WhisperAvailable() &&
               !Ai::FindWhisperModel().empty();
    };
    hooks.on_status = [this](const std::string& s, bool err) {
        SetStatus(s, err);
    };
    transcript_panel_->SetHooks(std::move(hooks));
}

}  // namespace XER::VideoEditor::Ui2
