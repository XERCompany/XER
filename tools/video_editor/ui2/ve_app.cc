#include "ve_app.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "monitor_panel.h"
#include "overlays/toast.h"
#include "rendering/renderer.h"
#include "shell_widgets.h"
#include "timeline_bridge.h"
#include "timeline_history.h"
#include "timeline_strip.h"
#include "timeline_surface.h"
#include "ve_theme.h"
#include "widgets/app_title_bar.h"
#include "widgets/command.h"
#include "widgets/dialog.h"
#include "widgets/resizable.h"
#include "widgets/sidebar.h"
#include "widgets/widget_tree.h"

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

VeApp::~VeApp() = default;

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
         {"Color", "C"}, {"Markers", "M", true}, {"Lab", "L"}});
    nav_rail_->SetOnSelect([this](int index) {
        // Rail presets — same WorkspaceLayout data as the switcher.
        static const char* kNames[] = {"Editing", "Effects", "Audio",
                                       "Color", "Markers", "Cut"};
        if (index >= 0 && index < 6) {
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
    left_dock_->AddTab("Media", "Media bin — catalog browser (M3)",
                       PH("dock.media", "Media",
                          "Catalog + bin list"));
    left_dock_->AddTab("Effects", "Effects browser (M3)",
                       PH("dock.effects", "Effects",
                          "Audio/video fx rack browser"));
    left_dock_->AddTab("Audio", "Audio tools (M3)",
                       PH("dock.audio", "Audio",
                          "Clip mixer + track controls"));
    left_dock_->AddTab("Text", "Titles & text (M3)",
                       PH("dock.text", "Text", "Title designer"));
    left_dock_->AddTab("Trans", "Transitions browser (M3)",
                       PH("dock.transitions", "Transitions",
                          "Transition presets"));

    right_dock_->AddTab("Inspector", "Clip inspector (M3)",
                        PH("dock.inspector", "Inspector",
                           "Selected clip properties"));
    right_dock_->AddTab("Color", "Color panel (M3)",
                        PH("dock.color", "Color",
                           "Wheels / curves / LUT"));
    right_dock_->AddTab("Markers", "Markers list (M3)",
                        PH("dock.markers", "Markers",
                           "Timeline marker table"));
    right_dock_->AddTab("History", "Undo history (M3)",
                        PH("dock.history", "History",
                           "TimelineHistory browser"));
    right_dock_->AddTab("Mix", "Audio mixer (M3)",
                        PH("dock.mix", "Mix",
                           "Track faders + meters"));
    right_dock_->AddTab("Queue", "Export queue (M3)",
                        PH("dock.queue", "Queue", "Render jobs"));
    right_dock_->AddTab("Ext", "Extensions (M3)",
                        PH("dock.ext", "Ext", "xer-ext host"));
    right_dock_->AddTab("Scopes", "Video scopes (M3)",
                        PH("dock.scopes", "Scopes",
                           "Waveform / vectorscope"));
    right_dock_->AddTab("Trn", "Transcript editor (M3)",
                        PH("dock.trn", "Trn",
                           "Text-based editing"));
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
    top_split_->AddPanel(RP(monitor_split_, c, 320.0f));
    top_split_->AddPanel(RP(right_dock_, r, 160.0f));
    SyncDockCollapsed();
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
         [this] { Notify("Import", "M2 — catalog import"); },
         true, false, false, true},
        {"Export…", "",
         [this] { Notify("Export", "M2 — export pipeline"); }},
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
    // on_click_point stays unset — the SmartSelect seam arms with
    // the M3 lab wiring (click points emit only while armed anyway).
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
    if (prefs_dirty_) {
        prefs_flush_timer_ += dt_seconds;
        if (prefs_flush_timer_ >= 1.0) {
            FlushPrefs();
        }
    }
}

void VeApp::TickTree(double dt_seconds) {
    if (auto* tree = GetTree(); tree != nullptr) {
        tree->Tick(dt_seconds);
    }
}

}  // namespace XER::VideoEditor::Ui2
