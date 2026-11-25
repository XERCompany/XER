#include "ve_app.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/ai_bounce.h"
#include "ai/audio_pcm.h"
#include "ai/face_detect.h"
#include "ai/rife_interp.h"
#include "ai/sam2_track.h"
#include "ai/upscale_x4v3.h"
#include "ai/whisper_transcribe.h"
#include "app_runtime/app_runtime.h"
#include "captions.h"
#include "cef/cef_ext_scheme.h"
#include "compositor.h"
#include "color_panel.h"
#include "effects_panel.h"
#include "export_dialog.h"
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
#include "project_settings_dialog.h"
#include "project_store.h"
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

// Nav-rail item -> workspace preset name (item order matches the
// SetItems list in BuildShell). Used both ways: rail click resolves
// the name, ApplyWorkspace syncs the rail's active highlight back.
constexpr std::array<const char*, 7> kRailWorkspaces = {
    "Editing", "Effects", "Audio", "Color", "Markers", "Photo", "Cut"};

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
    // `--open <file.xerproj>` + double-click launches: the ctor arg
    // is the real load path now (legacy parity — the arg used to be
    // a display-only prop). Autosave recovery is checked after the
    // bind so a crash's newer slot offers itself immediately.
    if (!project_path_.empty()) {
        LoadProject(project_path_);
    }
    CheckAutosaveRecovery();
    // Fresh/loaded documents match the file on disk — wiring pushes
    // (NotifyTimelineChanged during BuildShell) must not leave the
    // close guard armed.
    project_dirty_ = false;
    autosave_.ClearDirty();
    SyncAutosaveBadge();
}

VeApp::~VeApp() {
    // Worker teardown: the whisper job touches only its own
    // mutex-guarded slot, but the thread itself must not outlive the
    // members it captures — cancel + join before the fields go.
    transcript_cancel_ = true;
    if (transcript_worker_.joinable()) transcript_worker_.join();
    face_cancel_ = true;
    if (face_worker_.joinable()) face_worker_.join();
    bounce_cancel_ = true;
    if (bounce_worker_.joinable()) bounce_worker_.join();
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
    // Snap collapsed — the animated path leaves a half-width first
    // frame that reads as doubled, clipped glyphs in captures.
    nav_rail_->SetCollapsed(true, /*animate=*/false);
    nav_rail_->SetItems(
        {// {label, glyph, enabled, separator_before, icon_name}
         {"Edit", "E", true, false, "nav.media"},
         {"Fx", "F", true, false, "nav.fx"},
         {"Audio", "A", true, false, "nav.audio"},
         {"Color", "C", true, false, "fx.color"},
         {"Markers", "M", true, false, "nav.markers"},
         {"Photo", "P", true, false, "bin.kind.image"},
         {"Lab", "L", true, false, "tab.lab"}});
    nav_rail_->SetOnSelect([this](int index) {
        // Rail presets — same WorkspaceLayout data as the switcher.
        if (index >= 0 &&
            index < static_cast<int>(kRailWorkspaces.size())) {
            ApplyWorkspaceByName(kRailWorkspaces[index]);
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
                       media_panel_, "nav.media");
    left_dock_->AddTab("Effects", "Effects browser",
                       effects_panel_, "tab.effects");
    left_dock_->AddTab("Audio", "Audio tools (M3)",
                       PH("dock.audio", "Audio",
                          "Clip mixer + track controls"),
                       "nav.audio");
    left_dock_->AddTab("Text", "Titles & text (M3)",
                       PH("dock.text", "Text", "Title designer"),
                       "nav.text");
    left_dock_->AddTab("Trans", "Transitions browser (M3)",
                       PH("dock.transitions", "Transitions",
                          "Transition presets"),
                       "fx.overlay");

    right_dock_->AddTab("Inspector", "Clip inspector",
                        inspector_panel_, "tab.inspector");
    right_dock_->AddTab("Color", "Color workspace — wheels, tint, "
                                 "white balance",
                        color_panel_, "tool.color_wheel");
    right_dock_->AddTab("Markers", "Markers list — seek, edit, color",
                        markers_panel_, "tab.markers");
    right_dock_->AddTab("History", "Undo history — jump to any state",
                        history_panel_, "tab.history");
    right_dock_->AddTab("Mix", "Audio mixer — track faders, pan, "
                               "meters",
                        mixer_panel_, "tab.mixer");
    right_dock_->AddTab("Queue", "Export queue — render jobs",
                        queue_panel_, "tab.export");
    right_dock_->AddTab("Ext", "Extensions — xer-ext host",
                        extensions_panel_, "tab.extensions");
    right_dock_->AddTab("Scopes", "Video scopes — waveform, "
                                  "histogram, more",
                        scopes_panel_, "audio.waveform");
    right_dock_->AddTab("Trn", "Transcript editor — text-based "
                               "editing",
                        transcript_panel_, "bin.list");
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
    // A collapsed dock records a 0 share in the saved fractions
    // (collapsed panels own no span after RecomputeSizes). When the new
    // preset wants that dock visible again, a literal 0 would resurface
    // it at nothing but its min floor — fall back to the default share
    // so restore lands on real geometry.
    const bool have_saved = saved.size() == 3u;
    const float l =
        have_saved && saved[0] > 0.001f
            ? saved[0]
            : (left_visible_ ? 0.22f : 0.0f);
    const float r =
        have_saved && saved[2] > 0.001f
            ? saved[2]
            : (right_visible_ ? 0.26f : 0.0f);
    // The center share is the remainder by definition — a saved center
    // fraction is only meaningful alongside the dock shares it was
    // resolved with (a Photo session's near-1.0 center must not leak
    // into a restored two-dock row).
    const float c = std::max(0.2f, 1.0f - l - r);
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
         [this] { NewProject(); }},
        {"Open Project…", "Ctrl+O",
         [this] { OpenProjectInteractive(); }},
        {"Save Project", "Ctrl+S",
         [this] { SaveProjectInteractive(false); }},
        {"Save Project As…", "Ctrl+Shift+S",
         [this] { SaveProjectInteractive(true); }},
        {"Project Settings…", "",
         [this] { ShowProjectSettingsDialog(); },
         true, false, false, true},
        {"Import Media…", "",
         [this] { ImportMediaInteractive(); }},
        {"Export…", "",
         [this] { ShowExportDialog(); }},
        {"Recover Autosave", "",
         [this] { AcceptAutosaveRecovery(); },
         autosave_offer_, false, false, true},
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
        // The program gizmo re-pulls its target per paint — a
        // selection change must schedule one or the box lags a
        // frame behind the timeline's selection chrome.
        if (program_monitor_ != nullptr) {
            auto& surf = program_monitor_->VideoSurface();
            surf.Invalidate(surf.VideoAreaRect());
        }
        RefreshInspector();   // M3: selection provider re-pull
        RefreshTranscriptSubject();   // wave-3: selection rebinds the subject
    };
    host.on_seek = [this](double t) { OnBridgeSeek(t); };
    host.on_project_changed = [this] { NotifyTimelineChanged(); };
    host.on_snap_toggle = [this](bool enabled) {
        timeline_->SetSnapEnabled(enabled);
    };
    // Media seams: the catalog answers bounds (duration + has-video
    // feed every clip op's validation and the waveform's
    // seconds->bucket span via the bridge's get_media_bounds
    // default), the MediaPrep pool serves the tri-state peaks/poster
    // lookups, and dropped clips cancel their queued decodes. Every
    // lambda resolves the lazy stack at call time — a catalog that
    // failed to open still answers the documented fallbacks
    // (unbounded bounds / kFailed lookups), matching the legacy
    // catalog-miss path.
    host.bounds_for_path =
        [this](const std::string& path) -> MediaBounds {
        EnsureMediaStack();
        MediaBounds bounds;
        if (const CatalogEntry* e = FindCatalogEntry(path);
            e != nullptr) {
            bounds.duration_seconds = e->duration_seconds;
            bounds.has_video = e->HasVideo();
        } else if (media_prep_ != nullptr && !path.empty()) {
            // Catalog miss — queue the background probe so the NEXT
            // ask sees real bounds; this call stays unbounded.
            media_prep_->RequestProbe(path);
        }
        return bounds;
    };
    host.audio_peaks =
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
    host.posters =
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
    host.cancel_queued_media = [this](const std::string& path) {
        if (media_prep_ != nullptr) media_prep_->CancelQueued(path);
    };
    // AI seams — bound here (not in the bridge) so the ctx-menu
    // items appear only when the shell actually implements them.
    host.face_blur = [this](const std::string& clip_id) {
        StartFaceBlur(clip_id);
    };
    host.track_mask = [this](const std::string& clip_id) {
        StartTrackMask(clip_id);
    };
    host.clear_mask = [this](const std::string& clip_id) {
        ClearMaskTrack(clip_id);
    };
    host.ai_slowmo = [this](const std::string& clip_id) {
        StartAiSlowmo(clip_id);
    };
    // Render-status bar (Premiere's green/yellow/red strip): the
    // static cost class from the compositor's allowlist classifier,
    // upgraded to green (kCached) when the shared effect cache
    // already holds frames for the clip — i.e. the AutoCacher's
    // landed coverage answers "will this play?".
    host.clip_cost_class = [this](const Clip& clip) -> int {
        if (program_engine_.ClipCached(clip.id)) {
            return static_cast<int>(ClipCostClass::kCached);
        }
        return static_cast<int>(ClipCostClassFor(clip));
    };
    // Timeline ctx-menu edit verbs — RunSourceVerb dispatches onto
    // the targeted insert/overwrite/replace/fit-to-fill methods.
    host.source_verb =
        [this](Ui2::SourceEditVerb verb, double point,
               const std::string& clip_id) {
            RunSourceVerb(verb, point, clip_id);
        };
    // V/A patching badges — the shell owns the surface's targeted
    // set (requested state arrives here, the surface mirrors back).
    host.on_track_target = [this](int track, bool on) {
        if (const auto& s = timeline_->Surface()) {
            s->SetTrackTargeted(track, on);
        }
    };
    bridge_->SetHost(std::move(host));

    surface->SetHooks(bridge_->Hooks());
    surface->SetProject(&project_);
    surface->SetPlayheadSeconds(playhead_);
    NotifyTimelineChanged();
}

void VeApp::NotifyTimelineChanged() {
    // Legacy NotifyProjectChanged: repaint surfaces bound to the
    // document + arm the unsaved-changes state. Save/New/Load clear
    // the flags AFTER their own NotifyTimelineChanged — the flag
    // order there mirrors the old shell exactly (edit→dirty,
    // save→clean).
    project_dirty_ = true;
    autosave_.MarkDirty();
    playhead_ = std::clamp(playhead_, 0.0, project_.duration());
    if (timeline_ != nullptr) {
        if (const auto& s = timeline_->Surface()) {
            s->SetProject(&project_);
            s->SetSelectedClip(bridge_ != nullptr
                                   ? bridge_->SelectedClipId()
                                   : std::string{});
            s->SetPlayheadSeconds(playhead_);
            // Warm the async media seams for the (possibly new) clip
            // set — the lookups dedup against the prep pool and
            // surface caches, so this costs hash probes on repeat
            // calls and only enqueues genuinely-new decodes. Project
            // loads flow through SetProject -> here.
            s->PrimeMediaLookups();
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
        ActivateViewerScope(ShortcutRouter::kScopeProgram);
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
        // Armed pick seams share the monitor's click slot — the
        // SAM mask prompt takes precedence when armed (both can't
        // be armed at once; arming one disarms the other).
        if (track_mask_armed_) {
            track_mask_armed_ = false;
            program_monitor_->SetClickPointsArmed(false);
            const std::string clip_id = track_mask_clip_id_;
            track_mask_clip_id_.clear();
            const Clip* clip = FindClip(project_, clip_id);
            if (clip == nullptr) {
                SetStatus("Track Mask: clip is gone", true);
                return;
            }
            // Canvas point -> source-frame fractions through the
            // clip's effective transform at the displayed frame
            // (u = clip-local seconds; src_t = consumed source).
            const double u =
                std::clamp(playhead_ - clip->timeline_start, 0.0,
                           clip->duration);
            const double rate =
                std::isfinite(clip->playback_rate) &&
                        clip->playback_rate > 0.0
                    ? clip->playback_rate
                    : 1.0;
            const double src_t = clip->in_point + u * rate;
            int src_w = 0, src_h = 0;
            if (const CatalogEntry* e =
                    FindCatalogEntry(clip->source_path)) {
                src_w = e->width;
                src_h = e->height;
            }
            float fx = 0.0f, fy = 0.0f;
            if (src_w <= 0 || src_h <= 0 ||
                !Ai::CanvasPointToSourceFraction(
                    *clip, u, project_.width, project_.height,
                    src_w, src_h, nx, ny, &fx, &fy)) {
                SetStatus(
                    "Track Mask: click inside the clip's frame",
                    true);
                return;
            }
            BeginTrackMaskJob(*clip, fx, fy, src_t);
            return;
        }
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

    // Transform gizmo — program monitor only (the source monitor
    // never binds gizmo_target, so its surface draws no box). The
    // surface stays document-blind: it pulls the selection's
    // effective transform per paint, emits hook calls for pick /
    // select / preview / commit / reset, and the shell does the
    // document work here.
    {
        MonitorHooks& gh =
            program_monitor_->VideoSurface().MutableHooks();
        gh.gizmo_target = [this] {
            return GizmoTargetForSelection();
        };
        gh.gizmo_pick = [this](float nx, float ny) {
            return GizmoPickClip(nx, ny);
        };
        gh.on_gizmo_select = [this](const std::string& clip_id) {
            if (bridge_ != nullptr) bridge_->SelectClip(clip_id);
        };
        gh.on_gizmo_begin = [this](const char*) {
            // One undo step per gesture — the single ReplaceClip
            // commit lands inside this macro. The preview
            // compositor renders live frames off ITS bound
            // snapshot; project_ stays untouched until the commit.
            if (history_ != nullptr) {
                history_->BeginMacro("Monitor transform");
            }
            gizmo_preview_comp_ = std::make_unique<Compositor>();
            CompositorOptions options;
            if (proxies_ != nullptr && project_.use_proxies) {
                options.media_resolver =
                    [this](const std::string& path) {
                        return proxies_->ResolveDecodePath(path);
                    };
            }
            std::string error;
            gizmo_preview_ok_ =
                gizmo_preview_comp_->Init(project_, options, &error);
        };
        gh.on_gizmo_end = [this] {
            if (history_ != nullptr) history_->EndMacro();
            gizmo_preview_ok_ = false;
            if (gizmo_preview_comp_ != nullptr) {
                gizmo_preview_comp_->Shutdown();
                gizmo_preview_comp_.reset();
            }
        };
        gh.on_gizmo_preview =
            [this](const std::string& clip_id, const Transform& t) {
                GizmoPreviewTransform(clip_id, t);
            };
        gh.on_gizmo_commit =
            [this](const std::string& clip_id, const Transform& t) {
                CommitGizmoTransform(clip_id, t);
            };
        gh.on_gizmo_reset = [this](const std::string& clip_id) {
            const Clip* clip = FindClip(project_, clip_id);
            if (clip == nullptr) return;
            int w = 0, h = 0;
            if (!GizmoClipSourceSize(*clip, &w, &h)) return;
            // Restore the imported placement — the same letterbox
            // fit a fresh insert uses.
            CommitGizmoTransform(
                clip_id, FitTransformToCanvas(
                             w, h, project_.width, project_.height));
        };
        gh.gizmo_snap_enabled = [this] {
            return timeline_ != nullptr && timeline_->SnapEnabled();
        };
    }

    MonitorPanel::Hooks sh;
    sh.on_activate = [this] {
        ActivateViewerScope(ShortcutRouter::kScopeSource);
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

    // Initial pushed state — the document mirror fields + the
    // default program-scope accent border (router starts in the
    // program scope).
    NotifyTimelineChanged();
    ActivateViewerScope(ShortcutRouter::kScopeProgram);
}

void VeApp::ActivateViewerScope(const char* scope) {
    shortcuts_.ActivateScope(scope);
    const std::string_view s = scope != nullptr ? scope : "";
    if (program_monitor_ != nullptr) {
        program_monitor_->SetScopeActive(
            s == ShortcutRouter::kScopeProgram);
    }
    if (source_monitor_ != nullptr) {
        source_monitor_->SetScopeActive(
            s == ShortcutRouter::kScopeSource);
    }
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
    // Per-document patching (Premiere): a new track layout re-seeds
    // the default V1/A1 targets rather than inheriting stale
    // indices.
    if (timeline_ != nullptr && timeline_->Surface() != nullptr) {
        timeline_->Surface()->ResetTrackTargets();
    }
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
    ActivateViewerScope(ShortcutRouter::kScopeSource);
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
    // Track targeting (Premiere patching): the surface's V/A badge
    // set masks which tracks the range op touches — untargeted
    // lanes behave like locked ones.
    const std::vector<bool> mask =
        timeline_ != nullptr && timeline_->Surface() != nullptr
            ? timeline_->Surface()->TargetedMask()
            : std::vector<bool>{};
    std::string error;
    if (!history_->LiftRange(wi, wo, close_gap, &error, mask)) {
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

// ── held combos / play-around / go-to / match frame ────────────────
// Premiere transport extras riding the router's held-key contract:
// K held turns J/L into a fixed-rate slow shuttle (release either
// key → stop), Shift+K plays around the playhead, Shift+I/O goes to
// the in/out edge, F match-frames the clip under the playhead into
// the source monitor.

void VeApp::ProgramHeldShuttle(int direction) {
    // Exclusive playback — the engines share the audio endpoint.
    StopSourceTransport();
    held_shuttle_dir_ = direction;
    held_shuttle_source_ = false;
    program_engine_.HeldShuttle(
        direction * kHeldShuttleRate);
}

void VeApp::SourceHeldShuttle(int direction) {
    if (source_path_.empty()) {
        SetStatus("Source: no clip loaded");
        return;
    }
    StopProgramTransport();
    held_shuttle_dir_ = direction;
    held_shuttle_source_ = true;
    source_engine_.HeldShuttle(
        direction * kHeldShuttleRate);
}

void VeApp::EndHeldShuttle(int direction) {
    if (held_shuttle_dir_ == 0) return;
    if (direction != 0 && held_shuttle_dir_ != direction) return;
    const bool was_source = held_shuttle_source_;
    held_shuttle_dir_ = 0;
    // K still held + the OTHER shuttle key still down → the combo
    // keeps shuttling in that direction (releasing J under a held
    // K+L resumes forward slow shuttle).
    if (direction != 0 && shortcuts_.IsHeld('k')) {
        const int other = -direction;
        if (shortcuts_.IsHeld(other > 0 ? 'l' : 'j')) {
            if (was_source) {
                SourceHeldShuttle(other);
            } else {
                ProgramHeldShuttle(other);
            }
            return;
        }
    }
    (was_source ? source_engine_ : program_engine_).ShuttleStop();
}

void VeApp::ProgramPlayAround() {
    // Premiere Shift+K: seek back by the pre-roll, play, and pause at
    // the post-roll edge — the Tick pump owns the stop.
    StopSourceTransport();
    if (program_engine_.Playing()) program_engine_.ShuttleStop();
    const double dur = std::max(0.0, project_.duration());
    const double t = std::clamp(playhead_, 0.0, dur);
    playaround_until_ = std::min(dur, t + kPlayAroundPost);
    playaround_source_ = false;
    Seek(std::max(0.0, t - kPlayAroundPre));
    program_engine_.TogglePlay();
    SetStatus("Play around");
}

void VeApp::SourcePlayAround() {
    if (source_path_.empty()) {
        SetStatus("Source: no clip loaded");
        return;
    }
    StopProgramTransport();
    if (source_engine_.Playing()) source_engine_.ShuttleStop();
    const double t =
        std::clamp(source_playhead_, 0.0, source_duration_);
    playaround_until_ =
        std::min(source_duration_, t + kPlayAroundPost);
    playaround_source_ = true;
    SourceSeek(std::max(0.0, t - kPlayAroundPre));
    source_engine_.TogglePlay();
    SetStatus("Play around");
}

void VeApp::ProgramGoToWorkAreaEdge(bool mark_in) {
    // work_out <= work_in means "whole project" (model parity) —
    // the edge falls back to the document bounds.
    const bool has_area = project_.work_out > project_.work_in;
    const double target =
        mark_in ? (has_area ? project_.work_in : 0.0)
                : (has_area ? project_.work_out
                            : project_.duration());
    Seek(target);
    SetStatus(mark_in ? "Go to in point" : "Go to out point");
}

void VeApp::SourceGoToMark(bool mark_in) {
    if (source_path_.empty()) {
        SetStatus("Source: no clip loaded");
        return;
    }
    const auto it = source_marks_.find(source_path_);
    const double target =
        it != source_marks_.end()
            ? (mark_in ? it->second.in : it->second.out)
            : -1.0;
    if (target < 0.0) {
        SetStatus(mark_in ? "No source mark in"
                          : "No source mark out");
        return;
    }
    SourceSeek(target);
    SetStatus(mark_in ? "Go to mark in" : "Go to mark out");
}

void VeApp::ProgramMatchFrame() {
    // Premiere F: the timeline clip under the playhead loads into the
    // source monitor at the matching source time. Video tracks win
    // over audio; the topmost (lowest-indexed) track wins within a
    // kind. Compounds/generators have no media source to load.
    const Clip* best = nullptr;
    int best_pri = 0;
    for (const Clip& c : project_.clips) {
        if (c.source_path.empty() || !c.compound.empty()) continue;
        if (playhead_ < c.timeline_start ||
            playhead_ >= c.timeline_start + c.duration) {
            continue;
        }
        if (c.track < 0 ||
            c.track >= static_cast<int>(project_.tracks.size())) {
            continue;
        }
        const Track& tr = project_.tracks[c.track];
        if (tr.hidden) continue;
        const int pri =
            (tr.kind == TrackKind::Video ? 2 : 1) * 10000 - c.track;
        if (best == nullptr || pri > best_pri) {
            best = &c;
            best_pri = pri;
        }
    }
    if (best == nullptr) {
        SetStatus("Match frame: no clip under playhead");
        return;
    }
    const Track& tr = project_.tracks[best->track];
    // source t = in_point + (playhead - timeline_start) * rate
    // (Clip::playback_rate contract).
    const double src_t =
        best->in_point +
        (playhead_ - best->timeline_start) * best->playback_rate;
    // Probed metadata from the catalog when it knows the file;
    // clip/track-derived values otherwise.
    double dur = 0.0;
    int w = 0, h = 0;
    int fps_num = project_.fps_num > 0 ? project_.fps_num : 30;
    int fps_den = project_.fps_den > 0 ? project_.fps_den : 1;
    bool has_video = tr.kind == TrackKind::Video;
    bool has_audio = true;
    if (const CatalogEntry* e = FindCatalogEntry(best->source_path);
        e != nullptr) {
        dur = e->duration_seconds;
        w = e->width;
        h = e->height;
        has_video = e->HasVideo();
        has_audio = e->has_audio;
        if (e->fps > 0.0) {
            fps_num = static_cast<int>(e->fps * 1000.0 + 0.5);
            fps_den = 1000;
        }
    }
    if (w <= 0 || h <= 0) {
        w = project_.width;
        h = project_.height;
    }
    if (dur <= 0.0) {
        // No probe: the consumed source span is the best estimate.
        dur = best->in_point + best->duration * best->playback_rate;
    }
    std::string name =
        !best->name.empty() ? best->name : best->source_path;
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    OpenInSourceMonitor(best->source_path, name, dur, fps_num,
                        fps_den, has_video, has_audio, w, h);
    SourceSeek(std::clamp(src_t, 0.0, source_duration_));
    SetStatus("Match frame: " + name);
}

// Target badge name for status lines — "V2"/"A1" by per-kind
// ordinal (the surface header chip paints the same id).
static std::string TrackBadgeName(const Project& project,
                                  int track_index) {
    if (track_index < 0 ||
        track_index >= static_cast<int>(project.tracks.size())) {
        return "?";
    }
    const TrackKind kind =
        project.tracks[static_cast<std::size_t>(track_index)].kind;
    int ordinal = 0;
    for (int i = 0; i <= track_index; ++i) {
        if (project.tracks[static_cast<std::size_t>(i)].kind ==
            kind) {
            ++ordinal;
        }
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%d",
                  kind == TrackKind::Video ? 'V' : 'A', ordinal);
    return buf;
}

void VeApp::EditSourceToTimeline(bool insert, double at_override) {
    // Ported from VideoEditorAppWidget::EditSourceToTimeline — same
    // mark resolution, linked A/V pair, straddling split and history
    // macro. The destination now follows the surface's V/A patching
    // badges (Premiere source patching / Resolve destination
    // controls): the FIRST unlocked targeted track of the source's
    // kind wins; when that kind exists but nothing is targeted the
    // edit errors instead of guessing, and a patched-off audio leg
    // drops silently.
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
    TimelineSurface* surface =
        timeline_ != nullptr ? timeline_->Surface().get() : nullptr;
    const auto kind_exists = [this](TrackKind kind) {
        for (const Track& t : project_.tracks) {
            if (t.kind == kind) return true;
        }
        return false;
    };
    int track_index = -1;
    if (surface != nullptr) {
        track_index = surface->FirstTargetedTrack(wanted);
        if (track_index < 0 && kind_exists(wanted)) {
            SetStatus(std::string("Source edit failed: no targeted ") +
                          (wanted == TrackKind::Video ? "video"
                                                      : "audio") +
                          " track",
                      true);
            return;
        }
    } else {
        // No surface (legacy/headless) — the old selection-then-
        // first-unlocked fallback.
        const std::string selected =
            bridge_ != nullptr ? bridge_->SelectedClipId()
                               : std::string{};
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
    }
    if (track_index < 0) {
        track_index = history_->AddTrack(
            wanted, wanted == TrackKind::Video ? "Video" : "Audio");
        if (track_index < 0) {
            SetStatus("Source edit failed: cannot add a track", true);
            return;
        }
        // A kind that had no lane gets its first track targeted —
        // the patch set keeps up with auto-creation.
        if (surface != nullptr) {
            surface->SetTrackTargeted(track_index, true);
        }
    }
    int audio_track = -1;
    if (linked_av) {
        if (surface != nullptr) {
            audio_track = surface->FirstTargetedTrack(TrackKind::Audio);
            if (audio_track < 0 && !kind_exists(TrackKind::Audio)) {
                audio_track =
                    history_->AddTrack(TrackKind::Audio, "Audio");
                if (audio_track < 0) {
                    SetStatus("Source edit failed: cannot add an "
                              "audio track",
                              true);
                    return;
                }
                surface->SetTrackTargeted(audio_track, true);
            }
            // audio_track still < 0 here = every audio lane is
            // patched off or locked — the A leg drops (video-only
            // edit), matching Premiere's unpatched-A behavior.
        } else {
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
                    SetStatus("Source edit failed: cannot add an "
                              "audio track",
                              true);
                    return;
                }
            }
        }
    }
    const bool audio_leg = audio_track >= 0;

    // Premiere drops an insert at the playhead even when it lands
    // inside a clip — split the straddler first so InsertClipAt's
    // non-overlap validation passes.
    const bool snap =
        timeline_ != nullptr && timeline_->SnapEnabled();
    const double dest = at_override >= 0.0 ? at_override : playhead_;
    double at = snap ? SnapTime(project_, track_index, dest, 0.1,
                                playhead_)
                     : dest;
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
        insert && audio_leg ? straddler_on(audio_track) : nullptr;

    Clip clip = MakeSourceClip(source_path_, track_index, at, src_in,
                               src_out, source_media_w_,
                               source_media_h_, project_.width,
                               project_.height);
    clip.id = NewClipId();
    Clip audio_clip;
    if (audio_leg) {
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
    if (ok && audio_leg) {
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
    // Report the patch destination so the badges' effect is visible
    // in the status line ("Inserted onto V2+A1").
    std::string badge = TrackBadgeName(project_, track_index);
    if (audio_leg) {
        badge += "+" + TrackBadgeName(project_, audio_track);
    }
    SetStatus(std::string(insert ? "Inserted onto " : "Overwrote ") +
              badge + " — " + source_name_);
}

// ── source-edit verbs (replace / fit-to-fill / top-and-tail) ─────────
// The clip under the playhead on a TARGETED track — replace and
// top-and-tail both resolve their subject this way (Premiere's
// "clip on the patched track under the CTI"). Video-kind tracks win
// over audio; the lowest index wins within a kind. Locked tracks
// are skipped; an unwired surface accepts every track (legacy).
static const Clip* TargetedClipAt(const Project& project,
                                  const TimelineSurface* surface,
                                  double playhead) {
    const Clip* best = nullptr;
    int best_pri = 0;
    for (const Clip& c : project.clips) {
        if (playhead < c.timeline_start ||
            playhead >= c.timeline_start + c.duration) {
            continue;
        }
        if (c.track < 0 ||
            c.track >= static_cast<int>(project.tracks.size())) {
            continue;
        }
        const Track& tr = project.tracks[c.track];
        if (tr.locked) continue;
        if (surface != nullptr && !surface->TrackTargeted(c.track)) {
            continue;
        }
        const int pri =
            (tr.kind == TrackKind::Video ? 2 : 1) * 10000 - c.track;
        if (best == nullptr || pri > best_pri) {
            best = &c;
            best_pri = pri;
        }
    }
    return best;
}

void VeApp::SourceReplaceEdit(const Clip* target) {
    // Premiere "Replace Edit": the source range swaps into the
    // timeline clip's footprint — position, duration, effects and
    // link_group stay (history ReplaceClip keeps the clip id, so
    // transitions survive too). A source shorter than the footprint
    // clamps to its media bounds rather than overshooting.
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
    TimelineSurface* surface =
        timeline_ != nullptr ? timeline_->Surface().get() : nullptr;
    if (target == nullptr) {
        target =
            TargetedClipAt(project_, surface, playhead_);
    }
    if (target == nullptr) {
        SetStatus("Replace: no clip under the playhead on a targeted "
                  "track",
                  true);
        return;
    }
    double dur = target->duration;
    if (source_duration_ > 0.0) {
        dur = std::min(dur,
                       std::max(0.0, source_duration_ - src_in));
    }
    const double src_span = src_out - src_in;
    if (src_span < dur) dur = src_span;
    if (dur <= 1e-9) {
        SetStatus("Replace: source range is shorter than a frame",
                  true);
        return;
    }
    Clip repl = *target;
    repl.source_path = source_path_;
    if (!source_name_.empty()) repl.name = source_name_;
    repl.source.kind = SourceKind::Media;
    repl.in_point = src_in;
    repl.duration = dur;
    repl.playback_rate = 1.0;
    // The old clip's per-source annotations (tags, object/mask
    // tracks) describe different media — drop them; effects and
    // gain stay (the timeline treatment survives a replace).
    repl.tags.clear();
    repl.object_track = ClipTrack{};
    repl.mask_track = ClipMaskTrack{};
    const MediaBounds bounds{source_duration_, source_has_video_};
    std::string error;
    if (!history_->ReplaceClip(target->id, repl, bounds, &error)) {
        SetStatus("Replace failed: " + error, true);
        return;
    }
    if (bridge_ != nullptr) bridge_->SelectClip(target->id);
    NotifyTimelineChanged();
    SetStatus("Replaced " + TrackBadgeName(project_, target->track) +
              " clip with " + source_name_);
}

void VeApp::SourceFitToFill() {
    // Resolve "Fit to Fill": the marked source range retimes into
    // the fill range — work area when set, else the targeted clip
    // under the playhead's span — as one history macro.
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
    TimelineSurface* surface =
        timeline_ != nullptr ? timeline_->Surface().get() : nullptr;
    double fill_in = 0.0, fill_out = 0.0;
    if (project_.work_out > project_.work_in) {
        fill_in = project_.work_in;
        fill_out = project_.work_out;
    } else if (const Clip* under =
                   TargetedClipAt(project_, surface, playhead_);
               under != nullptr) {
        fill_in = under->timeline_start;
        fill_out = under->timeline_start + under->duration;
    } else {
        SetStatus("Fit to fill needs a work area or a clip under "
                  "the playhead",
                  true);
        return;
    }
    const double fill_dur = fill_out - fill_in;
    const double src_dur = src_out - src_in;
    if (fill_dur <= 1e-9 || src_dur <= 1e-9) {
        SetStatus("Fit to fill: empty range", true);
        return;
    }
    const double rate = src_dur / fill_dur;
    if (rate < 0.01 || rate > 100.0) {
        SetStatus("Fit to fill: rate out of range", true);
        return;
    }
    const TrackKind wanted =
        source_has_video_ ? TrackKind::Video : TrackKind::Audio;
    int track_index = -1;
    if (surface != nullptr) {
        track_index = surface->FirstTargetedTrack(wanted);
        if (track_index < 0) {
            bool kind_exists = false;
            for (const Track& t : project_.tracks) {
                if (t.kind == wanted) {
                    kind_exists = true;
                    break;
                }
            }
            if (kind_exists) {
                SetStatus("Fit to fill: no targeted track", true);
                return;
            }
        }
    } else {
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
            SetStatus("Fit to fill: cannot add a track", true);
            return;
        }
        if (surface != nullptr) {
            surface->SetTrackTargeted(track_index, true);
        }
    }

    Clip clip = MakeSourceClip(source_path_, track_index, fill_in,
                               src_in, src_out, source_media_w_,
                               source_media_h_, project_.width,
                               project_.height);
    clip.id = NewClipId();
    clip.playback_rate = rate;
    clip.duration = fill_dur;
    const MediaBounds bounds{source_duration_, source_has_video_};
    std::string error;
    history_->BeginMacro("Fit to fill");
    bool applied_any = false;
    bool ok = true;
    // Split straddlers at both fill edges so the overwrite covers
    // exactly the fill span.
    for (const double edge_t : {fill_in, fill_out}) {
        for (const Clip& c : project_.clips) {
            if (c.track != track_index ||
                c.timeline_start >= edge_t - 1e-9 ||
                ClipEndSeconds(c) <= edge_t + 1e-9) {
                continue;
            }
            std::string ignored;
            ok = history_->SplitClip(c.id, edge_t, &ignored);
            applied_any |= ok;
            if (!ok) {
                error = "fill-edge split failed";
                break;
            }
            break;   // one straddler per edge
        }
        if (!ok) break;
    }
    if (ok) {
        std::string new_id;
        ok = history_->OverwriteClipAt(clip, bounds, &new_id, &error);
        applied_any |= ok;
        if (ok) clip.id = new_id;
    }
    history_->EndMacro();
    if (!ok) {
        if (applied_any) history_->Undo();
        SetStatus("Fit to fill failed: " + error, true);
        return;
    }
    if (bridge_ != nullptr) bridge_->SelectClip(clip.id);
    NotifyTimelineChanged();
    char rate_txt[24];
    std::snprintf(rate_txt, sizeof(rate_txt), "%.2fx", rate);
    SetStatus(std::string("Fit to fill ") + rate_txt + " onto " +
              TrackBadgeName(project_, track_index));
}

void VeApp::RunSourceVerb(Ui2::SourceEditVerb verb, double point,
                          const std::string& clip_id) {
    switch (verb) {
        case Ui2::SourceEditVerb::kInsert:
            EditSourceToTimeline(true, point);
            break;
        case Ui2::SourceEditVerb::kOverwrite:
            EditSourceToTimeline(false, point);
            break;
        case Ui2::SourceEditVerb::kReplace: {
            const Clip* target = FindClip(project_, clip_id);
            if (target == nullptr) {
                SetStatus("Replace: no clip at that point", true);
                return;
            }
            SourceReplaceEdit(target);
            break;
        }
        case Ui2::SourceEditVerb::kFitToFill:
            SourceFitToFill();
            break;
    }
}

void VeApp::ProgramTopTail(bool head) {
    // Premiere Q/W: the clip edge under the playhead on the targeted
    // track ripples to the playhead — head trims its start, tail its
    // end, and the freed gap closes (history_->RippleTrimClip).
    TimelineSurface* surface =
        timeline_ != nullptr ? timeline_->Surface().get() : nullptr;
    const Clip* clip = TargetedClipAt(project_, surface, playhead_);
    if (clip == nullptr) {
        SetStatus("Top-and-tail: no clip under the playhead on a "
                  "targeted track",
                  true);
        return;
    }
    if (history_ == nullptr) return;
    const double rate =
        clip->playback_rate != 0.0 ? clip->playback_rate : 1.0;
    const double src_point =
        clip->in_point + (playhead_ - clip->timeline_start) * rate;
    std::string error;
    if (!history_->RippleTrimClip(
            clip->id, head ? TrimEdge::In : TrimEdge::Out, src_point,
            [this](const std::string& path) {
                MediaBounds bounds;
                if (const CatalogEntry* e = FindCatalogEntry(path);
                    e != nullptr) {
                    bounds.duration_seconds = e->duration_seconds;
                    bounds.has_video = e->HasVideo();
                }
                return bounds;
            },
            &error)) {
        SetStatus("Top-and-tail failed: " + error, true);
        return;
    }
    NotifyTimelineChanged();
    SetStatus(head ? "Trimmed start to playhead"
                   : "Trimmed end to playhead");
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
    // PreviewBinding pair carries the ±{1,2,4,8} ladder. All three
    // are HELD bindings now: the router records the press and routes
    // the release to the recorded binding (K+J/K+L held slow shuttle
    // stops wherever the scope moved; a direction-key release while K
    // stays down ends that shuttle — Premiere parity).
    shortcuts_.BindHeldKey('j', [this] {
                               if (shortcuts_.IsHeld('k')) {
                                   SourceHeldShuttle(-1);
                               } else {
                                   SourceShuttleStep(-1);
                               }
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(-1);
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindHeldKey('k', [this] {
                               SourceShuttleStop();
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(0);
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindHeldKey('l', [this] {
                               if (shortcuts_.IsHeld('k')) {
                                   SourceHeldShuttle(1);
                               } else {
                                   SourceShuttleStep(1);
                               }
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(1);
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindHeldKey('j', [this] {
                               if (shortcuts_.IsHeld('k')) {
                                   ProgramHeldShuttle(-1);
                               } else {
                                   ProgramShuttleStep(-1);
                               }
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(-1);
                               return true;
                           },
                           R::kScopeProgram);
    shortcuts_.BindHeldKey('k', [this] {
                               ProgramShuttleStop();
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(0);
                               return true;
                           },
                           R::kScopeProgram);
    shortcuts_.BindHeldKey('l', [this] {
                               if (shortcuts_.IsHeld('k')) {
                                   ProgramHeldShuttle(1);
                               } else {
                                   ProgramShuttleStep(1);
                               }
                               return true;
                           },
                           [this] {
                               EndHeldShuttle(1);
                               return true;
                           },
                           R::kScopeProgram);
    // Shift+K play-around — pre-roll seek + timed play (the Tick pump
    // stops at the post-roll edge).
    shortcuts_.BindShiftKey('k', [this] {
                               SourcePlayAround();
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindShiftKey('k', [this] {
                               ProgramPlayAround();
                               return true;
                           },
                           R::kScopeProgram);
    // F — match frame: clip under the playhead loads into the source
    // monitor at the matching source time (global: reads program
    // state regardless of the active viewer).
    shortcuts_.BindKey('f', [this] {
                           ProgramMatchFrame();
                           return true;
                       });
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
    // Shift+I/Shift+O — go to the mark (source) / work-area edge
    // (program), no mark write.
    shortcuts_.BindShiftKey('i', [this] {
                               SourceGoToMark(true);
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindShiftKey('o', [this] {
                               SourceGoToMark(false);
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
    shortcuts_.BindShiftKey('i', [this] {
                               ProgramGoToWorkAreaEdge(true);
                               return true;
                           },
                           R::kScopeProgram);
    shortcuts_.BindShiftKey('o', [this] {
                               ProgramGoToWorkAreaEdge(false);
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
    // Top-and-tail (Premiere Q/W): ripple-trim the targeted clip's
    // start/end to the playhead. Program scope.
    shortcuts_.BindKey('q', [this] {
                           ProgramTopTail(true);
                           return true;
                       },
                       R::kScopeProgram);
    shortcuts_.BindKey('w', [this] {
                           ProgramTopTail(false);
                           return true;
                       },
                       R::kScopeProgram);
    // Source-scope verb chords: Shift+, replaces the targeted clip
    // under the playhead with the source range; Shift+. fits the
    // source range to the work area (Fit to Fill). F9–F12 don't
    // exist in KeyCode, so the punctuation row carries them.
    shortcuts_.BindShiftKey(',', [this] {
                               SourceReplaceEdit();
                               return true;
                           },
                           R::kScopeSource);
    shortcuts_.BindShiftKey('.', [this] {
                               SourceFitToFill();
                               return true;
                           },
                           R::kScopeSource);
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
                         [this] {
                             SaveProjectInteractive(false);
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::S,
                         [this] {
                             SaveProjectInteractive(true);
                             return true;
                         },
                         /*ctrl=*/true, /*shift=*/true);
    shortcuts_.BindChord(W::KeyCode::O,
                         [this] {
                             OpenProjectInteractive();
                             return true;
                         },
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::N,
                         [this] {
                             NewProject();
                             return true;
                         },
                         /*ctrl=*/true);
    // (No export chord — KeyCode has no M; Export lives on the File
    // menu + the queue's "+ Enqueue" button.)
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
    if (nav_rail_ != nullptr) {
        // Keep the rail highlight honest when a preset lands via the
        // menu/palette/shortcuts rather than a rail click.
        for (std::size_t i = 0; i < kRailWorkspaces.size(); ++i) {
            if (ws.name == kRailWorkspaces[i]) {
                nav_rail_->SetActiveIndex(static_cast<int>(i));
                break;
            }
        }
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
    // Shift+K play-around: pause the owning binding when its playhead
    // reaches the post-roll edge; an early stop (user brake, engine
    // auto-stop at the end) just clears the pending window.
    if (playaround_until_ >= 0.0) {
        PreviewBinding& bound = playaround_source_ ? source_engine_
                                                 : program_engine_;
        if (!bound.Playing() ||
            bound.Playhead() >= playaround_until_) {
            if (bound.Playing()) bound.ShuttleStop();
            playaround_until_ = -1.0;
        }
    }
    // Media-prep drains: poster/peaks results repaint the bin
    // (NotifyMediaChanged clears negative caches + invalidates), an
    // import poster writing poster_path refreshes the row data.
    if (media_prep_ != nullptr) {
        if (media_prep_->ConsumeRenderDirty()) {
            // Poster/peaks results repaint BOTH async consumers: the
            // bin grid and the timeline surface (its NotifyMediaChanged
            // clears the failed/zero-probe caches so late-arriving
            // lookups finally stick, then invalidates).
            if (media_panel_ != nullptr) {
                media_panel_->NotifyMediaChanged();
            }
            if (timeline_ != nullptr && timeline_->Surface() != nullptr) {
                timeline_->Surface()->NotifyMediaChanged();
            }
        }
        if (media_prep_->ConsumeCatalogDirty()) {
            RefreshBinEntries();
        }
        const std::uint64_t dropped =
            media_prep_->ConsumeDroppedPosterRequests() +
            media_prep_->ConsumeDroppedProbeRequests();
        if (dropped > 0) {
            SetStatus("Media queue full — " +
                          std::to_string(dropped) +
                          " prep request(s) dropped",
                      true);
        }
    }
    // Wave-3 drivers: the export engine's Progress()/Status() drain
    // into queue rows, the whisper worker's result slot into the
    // transcript panel + cache. Both are poll-seams — the workers
    // never touch widgets.
    PumpExportQueue(dt_seconds);
    PumpTranscriptJob();
    PumpFaceBlurJob();
    PumpTrackMaskJob();
    PumpAiBounceJob();
    // Autosave rides the same ~2 s poll cadence the old shell used —
    // the manager gates on dirty + interval before touching disk.
    PumpAutosave(dt_seconds);
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
    hooks.on_ai_upscale = [this](const std::string& path) {
        StartAiUpscale(path);
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

// ---- program-monitor transform gizmo ------------------------------------
//
// The surface is document-blind: these hooks resolve the bridge
// selection into a gizmo target (effective transform at the
// playhead — the box tracks what the user actually sees), map
// presses onto the topmost clip under the cursor, and funnel the
// gesture through the inspector's ReplaceClip seam so a drag lands
// as exactly one undo step.
//
// Live preview renders through gizmo_preview_comp_ (one Compositor
// per gesture, Init'd at on_gizmo_begin): UpdateClipTransform
// mutates ITS bound snapshot — project_ stays untouched until the
// commit, so an Esc cancel needs no document rollback at all.

bool VeApp::GizmoClipSourceSize(const Clip& clip, int* w,
                                int* h) const {
    if (w == nullptr || h == nullptr) return false;
    if (!clip.source_path.empty()) {
        if (const CatalogEntry* e = FindCatalogEntry(clip.source_path);
            e != nullptr && e->width > 0 && e->height > 0) {
            *w = e->width;
            *h = e->height;
            return true;
        }
        return false;   // media clip, dims unknown — no box
    }
    // Generated/compound/adjustment sources composite at canvas
    // scale (matte/bars adopt the canvas as their natural size; the
    // box is a touch generous for small Text clips — cosmetic).
    if (project_.width > 0 && project_.height > 0) {
        *w = project_.width;
        *h = project_.height;
        return true;
    }
    return false;
}

MonitorGizmoTarget VeApp::GizmoTargetForSelection() const {
    MonitorGizmoTarget t;
    if (bridge_ == nullptr) return t;
    const std::string& id = bridge_->SelectedClipId();
    if (id.empty()) return t;
    const Clip* clip = FindClip(project_, id);
    if (clip == nullptr) return t;
    const Track* track = FindTrack(project_, clip->track);
    if (track == nullptr || track->kind != TrackKind::Video ||
        track->hidden || track->locked) {
        return t;   // audio/hidden/locked clips get no gizmo
    }
    if (!(playhead_ >= clip->timeline_start &&
          playhead_ < ClipEndSeconds(*clip))) {
        return t;   // selected clip isn't under the playhead
    }
    if (!GizmoClipSourceSize(*clip, &t.src_w, &t.src_h)) return t;
    const double u = std::clamp(playhead_ - clip->timeline_start,
                                0.0, clip->duration);
    t.clip_id = id;
    t.transform = EffectiveTransformAt(*clip, u);
    t.canvas_w = project_.width;
    t.canvas_h = project_.height;
    return t;
}

std::string VeApp::GizmoPickClip(float nx, float ny) const {
    if (nx < 0.0f || nx > 1.0f || ny < 0.0f || ny > 1.0f ||
        project_.width <= 0 || project_.height <= 0) {
        return {};
    }
    // Topmost-first: the compositor paints video tracks bottom→top
    // in index order, so the LAST video track owns the topmost
    // pixels. Hidden tracks contribute nothing to pick against.
    for (int ti = static_cast<int>(project_.tracks.size()) - 1;
         ti >= 0; --ti) {
        const Track& track =
            project_.tracks[static_cast<std::size_t>(ti)];
        if (track.kind != TrackKind::Video || track.hidden) {
            continue;
        }
        for (const Clip& clip : project_.clips) {
            if (clip.track != ti ||
                !(playhead_ >= clip.timeline_start &&
                  playhead_ < ClipEndSeconds(clip))) {
                continue;
            }
            int w = 0, h = 0;
            if (!GizmoClipSourceSize(clip, &w, &h)) continue;
            const double u = std::clamp(
                playhead_ - clip.timeline_start, 0.0, clip.duration);
            float fx = 0.0f, fy = 0.0f;
            // Exact inverse of the warp map — true only when the
            // canvas point lands inside the clip's warped rect.
            if (Ai::CanvasPointToSourceFraction(
                    clip, u, project_.width, project_.height, w, h,
                    nx, ny, &fx, &fy)) {
                return clip.id;
            }
        }
    }
    return {};
}

void VeApp::GizmoPreviewTransform(const std::string& clip_id,
                                  const Transform& transform) {
    if (!gizmo_preview_ok_ || gizmo_preview_comp_ == nullptr ||
        program_monitor_ == nullptr) {
        return;
    }
    if (!gizmo_preview_comp_->UpdateClipTransform(clip_id,
                                                  transform)) {
        return;
    }
    std::vector<std::uint8_t> bgra;
    std::string error;
    if (!gizmo_preview_comp_->RenderFrameAt(playhead_, &bgra,
                                            &error)) {
        return;   // decode hiccup — the box still tracks the drag
    }
    program_monitor_->SetFrame(bgra, project_.width,
                               project_.height);
}

void VeApp::CommitGizmoTransform(const std::string& clip_id,
                                 const Transform& transform) {
    if (history_ == nullptr) return;
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    Clip updated = *clip;
    // The surface commits the effective-at-playhead transform back
    // through the static fields — the inspector's semantics
    // (keyframed props keep animating over the new base value).
    updated.transform = transform;
    CommitInspectorClips({updated});
    // Repaint the paused monitor: inside a gesture the preview
    // compositor is still warm; outside one (double-click reset)
    // open a one-shot so the commit is visible without a transport
    // action (the engine binding re-inits on its next transport
    // action either way — inspector-edit parity).
    if (gizmo_preview_ok_) {
        GizmoPreviewTransform(clip_id, transform);
    } else if (program_monitor_ != nullptr) {
        Compositor comp;
        CompositorOptions options;
        if (proxies_ != nullptr && project_.use_proxies) {
            options.media_resolver =
                [this](const std::string& path) {
                    return proxies_->ResolveDecodePath(path);
                };
        }
        std::string error;
        std::vector<std::uint8_t> bgra;
        if (comp.Init(project_, options, &error) &&
            comp.RenderFrameAt(playhead_, &bgra, &error)) {
            program_monitor_->SetFrame(bgra, project_.width,
                                       project_.height);
        }
    }
    RefreshInspector();
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
        // First writer wins on the shared click seam — drop any
        // pending mask prompt so the next click is a color sample.
        track_mask_armed_ = false;
        track_mask_clip_id_.clear();
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
    // "+ Enqueue" opens the export-settings dialog — the job is
    // composed on submit (EnqueueExportJob), which also captures the
    // per-job ExportOptions the driver consumes.
    hooks.on_enqueue = [this] { ShowExportDialog(); };
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
    // The export snapshot is normally the live project — dialog-
    // submitted jobs carry an ExportRequest that may rewrite fps/
    // resolution/range/codec fields (fps + canvas ride the snapshot,
    // matching the engine's project-derived contract).
    Project export_project = project_;
    const auto req_it = export_requests_.find(job_id);
    if (req_it != export_requests_.end()) {
        const ExportRequest& r = req_it->second;
        options.video_codec = r.video_codec;
        options.video_bit_rate = r.video_bit_rate;
        options.video_crf = r.video_crf;
        options.audio_codec = r.audio_codec;
        options.audio_bit_rate = r.audio_bit_rate;
        options.audio_sample_rate = r.audio_sample_rate;
        if (r.range == ExportRequest::Range::Custom) {
            options.range_in = r.range_in;
            options.range_out = r.range_out;
        } else if (r.range == ExportRequest::Range::WholeTimeline) {
            // A nonzero explicit range overrides the work area —
            // {0, duration} pins "entire sequence".
            options.range_in = 0.0;
            options.range_out = export_project.duration();
        }   // WorkArea: {0,0} — the engine resolves the work area
        if (r.fps_num > 0 && r.fps_den > 0) {
            export_project.fps_num = r.fps_num;
            export_project.fps_den = r.fps_den;
        }
        if (r.width > 0 && export_project.width > 0 &&
            r.width != export_project.width) {
            // Explicit w/h maps onto the engine's uniform canvas
            // scale (height follows the same factor — the form's
            // estimate already shows the resolved pair).
            options.resolution_scale =
                static_cast<double>(r.width) /
                static_cast<double>(export_project.width);
        }
    }
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
    if (!export_engine_->Start(export_project, options, {}, &error)) {
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

// ── project lifecycle ─────────────────────────────────────────────
// Legacy parity: VideoEditorAppWidget owned project_dirty_ +
// ConfirmDiscardChanges + the autosave manager; here the same three
// pieces hang off the shell — the dirty flag is armed by
// NotifyTimelineChanged (the single mutation funnel) and cleared by
// the save/load/new paths below.

void VeApp::RequestCloseGuard(std::function<void()> proceed) {
    RunCloseGuard(std::move(proceed), /*for_window_close=*/false);
}

void VeApp::RunCloseGuard(std::function<void()> proceed,
                          bool for_window_close) {
    if (!project_dirty_) {
        if (for_window_close) close_approved_ = true;
        if (proceed) proceed();
        return;
    }
    if (close_guard_open_) return;   // one prompt at a time
    close_guard_open_ = true;
    close_guard_for_window_ = for_window_close;
    close_guard_proceed_ = std::move(proceed);

    const std::string name =
        project_path_.empty()
            ? (project_name_.empty() ? "Untitled.xerproj"
                                     : project_name_ + ".xerproj")
            : [&] {
                  const std::size_t s =
                      project_path_.find_last_of("/\\");
                  return s == std::string::npos
                             ? project_path_
                             : project_path_.substr(s + 1);
              }();
    auto body = PlaceholderPanel::Create(
        "close.guard", "",
        "Save changes to \"" + name +
            "\"?\nYour edits will be lost if you don't save.");
    close_guard_dialog_ =
        W::Dialog::Create("Unsaved changes", body, "", "");
    std::vector<W::Dialog::Button> buttons;
    buttons.push_back({"Save", W::ButtonStyle::Primary,
                       W::Dialog::Result::None,
                       [this] {
                           ResolveCloseGuard(CloseGuardChoice::Save);
                       }});
    buttons.push_back({"Don't Save", W::ButtonStyle::Secondary,
                       W::Dialog::Result::None,
                       [this] {
                           ResolveCloseGuard(
                               CloseGuardChoice::Discard);
                       }});
    buttons.push_back({"Cancel", W::ButtonStyle::Secondary,
                       W::Dialog::Result::Rejected, nullptr});
    close_guard_dialog_->SetButtons(std::move(buttons));
    close_guard_dialog_->SetOnClosed([this] {
        HideOverlay();
        // Dismissal paths (Escape, scrim, the X) count as Cancel —
        // the pending op is dropped with the prompt.
        close_guard_open_ = false;
        close_guard_for_window_ = false;
        close_guard_proceed_ = nullptr;
        close_guard_dialog_ = nullptr;
    });
    ShowOverlay(close_guard_dialog_);
}

void VeApp::ResolveCloseGuard(CloseGuardChoice choice) {
    if (!close_guard_open_) return;
    switch (choice) {
        case CloseGuardChoice::Save:
            // A cancelled/failed save aborts the destructive op too —
            // legacy SaveProject(false) semantics.
            if (!SaveProjectInteractive(false)) return;
            break;
        case CloseGuardChoice::Discard:
            break;
        case CloseGuardChoice::Cancel: {
            close_guard_open_ = false;
            close_guard_for_window_ = false;
            close_guard_proceed_ = nullptr;
            if (close_guard_dialog_ != nullptr) {
                close_guard_dialog_->Close();   // OnClosed resets state
            }
            return;
        }
    }
    auto proceed = std::move(close_guard_proceed_);
    const bool for_window = close_guard_for_window_;
    close_guard_proceed_ = nullptr;
    close_guard_open_ = false;
    close_guard_for_window_ = false;
    if (close_guard_dialog_ != nullptr) {
        close_guard_dialog_->Close();
    } else {
        HideOverlay();
    }
    if (for_window) close_approved_ = true;
    if (proceed) proceed();
    if (for_window && on_close_approved_ != nullptr) {
        on_close_approved_();
    }
}

bool VeApp::RequestClose() {
    // One-shot latch: the approval resolves exactly one real close —
    // consume it so a later dirty state re-arms the guard.
    if (close_approved_) {
        close_approved_ = false;
        return true;
    }
    if (!project_dirty_) return true;
    RunCloseGuard([] {}, /*for_window_close=*/true);
    return false;
}

void VeApp::SetOnCloseApproved(std::function<void()> callback) {
    on_close_approved_ = std::move(callback);
}

void VeApp::NewProject() {
    // Legacy order: refuse while an export runs, then the
    // save/discard/cancel prompt gates the reset.
    if (export_engine_ != nullptr && export_engine_->IsRunning()) {
        SetStatus("Wait for export to finish", true);
        return;
    }
    RunCloseGuard([this] { DoNewProject(); },
                  /*for_window_close=*/false);
}

void VeApp::DoNewProject() {
    if (history_ != nullptr) history_->Clear();
    if (media_prep_ != nullptr) media_prep_->CancelAllQueued();
    project_ = Project{};
    project_path_.clear();
    project_name_.clear();
    playhead_ = 0.0;
    // Rebuild the document pair — the bridge is rebound so the
    // surface's hooks point at the fresh history (the old shell's
    // NewProject ran the same reset).
    EnsureTimelineDocument();
    WireTimelineBridge();
    NotifyTimelineChanged();
    autosave_ = XER::VideoEditor::AutosaveManager{};
    project_dirty_ = false;   // fresh canvas — nothing to lose
    autosave_.ClearDirty();
    SyncAutosaveBadge();
    SetStatus("New project");
    RebuildMenus();
}

bool VeApp::LoadProject(const std::string& utf8_path) {
    std::string error;
    if (history_ != nullptr) history_->Clear();
    if (media_prep_ != nullptr) media_prep_->CancelAllQueued();
    Project loaded;
    if (!XER::VideoEditor::ProjectStore::Load(
            XER::Helper::Path::NormalizeUtf8Path(utf8_path), &loaded,
            &error)) {
        SetStatus("Open failed: " + error, true);
        return false;
    }
    project_ = std::move(loaded);
    if (project_.tracks.empty()) {
        EnsureTimelineDocument();   // seed the editable grid
    }
    project_path_ =
        XER::Helper::Path::NormalizeUtf8Path(utf8_path);
    playhead_ = 0.0;
    NotifyTimelineChanged();
    InitAutosave();
    CheckAutosaveRecovery();
    project_dirty_ = false;   // matches the file on disk
    autosave_.ClearDirty();
    SyncAutosaveBadge();
    SetStatus("Opened " +
              std::filesystem::path(project_path_)
                  .filename()
                  .string());
    RebuildMenus();
    return true;
}

void VeApp::OpenProjectInteractive() {
    // Guard first — a cancelled prompt must not strand the picker on
    // an op the user just aborted (legacy OpenProject parity).
    RunCloseGuard(
        [this] {
#if defined(_WIN32)
            wchar_t buffer[1024] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
            ofn.lpstrFilter =
                L"XER project\0*.xerproj\0All files\0*.*\0";
            ofn.lpstrTitle = L"Open project";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                        OFN_NOCHANGEDIR;
            if (!::GetOpenFileNameW(&ofn)) return;
            LoadProject(
                XER::Helper::String::WideToUtf8(buffer));
#else
            SetStatus("Open dialog is Windows-only", true);
#endif
        },
        /*for_window_close=*/false);
}

bool VeApp::SaveProject(bool choose_path) {
    if (project_path_.empty() || choose_path) {
#if defined(_WIN32)
        const std::wstring suggested = XER::Helper::String::Utf8ToWide(
            project_name_.empty() ? "untitled.xerproj"
                                  : project_name_ + ".xerproj");
        wchar_t buffer[1024] = {};
        wcsncpy_s(buffer, suggested.c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
        ofn.lpstrFilter =
            L"XER project\0*.xerproj\0All files\0*.*\0";
        ofn.lpstrTitle = L"Save project";
        ofn.lpstrDefExt = L"xerproj";
        ofn.Flags =
            OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!::GetSaveFileNameW(&ofn)) return false;
        project_path_ = XER::Helper::Path::NormalizeUtf8Path(
            XER::Helper::String::WideToUtf8(buffer));
#else
        SetStatus("Save dialog is Windows-only", true);
        return false;
#endif
    }
    std::string error;
    if (!XER::VideoEditor::ProjectStore::Save(project_, project_path_,
                                              &error)) {
        SetStatus("Save failed: " + error, true);
        return false;
    }
    InitAutosave();           // bind to a first-saved/renamed path
    autosave_.ClearDirty();
    project_dirty_ = false;
    SyncAutosaveBadge();
    SetStatus("Saved " +
              std::filesystem::path(project_path_)
                  .filename()
                  .string());
    RebuildMenus();
    return true;
}

bool VeApp::SaveProjectInteractive(bool choose_path) {
    return SaveProject(choose_path);
}

// Headless save seam for the smoke harness — no picker, no guard.
bool VeApp::SaveProjectAs(const std::string& utf8_path) {
    project_path_ =
        XER::Helper::Path::NormalizeUtf8Path(utf8_path);
    return SaveProject(false);
}

// ── application dialogs ─────────────────────────────────────────────

void VeApp::ShowExportDialog() {
    export_form_ = ExportDialog::Create();
    export_form_->SetProject(&project_);
    ExportRequest seed;
    if (!project_path_.empty()) {
        std::string stem =
            std::filesystem::path(project_path_)
                .filename()
                .string();
        const std::size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem.resize(dot);
        std::filesystem::path dir =
            !prefs_.scratch_dir.empty()
                ? std::filesystem::path(prefs_.scratch_dir)
                : std::filesystem::path(project_path_).parent_path();
        seed.output_path =
            (dir / (stem + ".mp4")).string();
    } else {
        std::error_code ec;
        const auto dir =
            !prefs_.scratch_dir.empty()
                ? std::filesystem::path(prefs_.scratch_dir)
                : std::filesystem::temp_directory_path(ec) /
                      "xer_exports";
        seed.output_path =
            (dir / "untitled.mp4").string();
    }
    export_form_->SetRequest(seed);
    export_form_->SetBrowseHandler(
        [](const std::string& extension,
           const std::string& suggested) -> std::string {
#if defined(_WIN32)
            const std::wstring filter =
                std::wstring(L"Export (*.") +
                XER::Helper::String::Utf8ToWide(extension) +
                L")\0*." +
                XER::Helper::String::Utf8ToWide(extension) +
                L"\0All files\0*.*\0";
            wchar_t buffer[1024] = {};
            const std::wstring wide =
                XER::Helper::String::Utf8ToWide(
                    suggested.empty() ? std::string("export.") +
                                            extension
                                      : suggested);
            wcsncpy_s(buffer, wide.c_str(), _TRUNCATE);
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
            ofn.lpstrFilter = filter.c_str();
            ofn.lpstrTitle = L"Export media";
            const std::wstring wide_ext =
                XER::Helper::String::Utf8ToWide(extension);
            ofn.lpstrDefExt = wide_ext.c_str();
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                        OFN_NOCHANGEDIR;
            if (!::GetSaveFileNameW(&ofn)) return {};
            return XER::Helper::Path::NormalizeUtf8Path(
                XER::Helper::String::WideToUtf8(buffer));
#else
            (void)extension;
            (void)suggested;
            return {};
#endif
        });
    export_dialog_ =
        W::Dialog::Create("Export", export_form_, "Export", "Cancel");
    export_dialog_->SetPanelWidth(470.0f);
    export_dialog_->SetPanelHeight(500.0f);
    export_dialog_->SetMovable(true);
    export_dialog_->SetResizable(true);
    std::weak_ptr<ExportDialog> weak_form = export_form_;
    export_dialog_->SetOnResult(
        [this, weak_form](W::Dialog::Result r) {
            if (r == W::Dialog::Result::Accepted) {
                SubmitExportForm();
            }
        });
    export_dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(export_dialog_);
    // Reveal the Queue tab so the job row is visible on submit.
    if (right_dock_ != nullptr) {
        for (int i = 0; i < right_dock_->TabCount(); ++i) {
            if (right_dock_->PanelAt(i).get() ==
                queue_panel_.get()) {
                right_dock_->SetActiveIndex(i);
                break;
            }
        }
    }
}

void VeApp::SubmitExportForm() {
    if (export_form_ == nullptr) return;
    std::string error = export_form_->Validate();
    const ExportRequest request = export_form_->GetRequest();
    if (!error.empty()) {
        SetStatus("Export: " + error, true);
        return;   // dialog stays open with the error line set
    }
    if (!EnqueueExportJob(request, &error)) {
        export_form_->SetOutputPath(request.output_path);
        SetStatus("Export: " + error, true);
        return;
    }
    last_export_request_ = request;
    if (export_dialog_ != nullptr) export_dialog_->Close();
}

bool VeApp::EnqueueExportJob(const ExportRequest& request,
                             std::string* error_out) {
    if (queue_panel_ == nullptr) {
        if (error_out != nullptr) *error_out = "queue unavailable";
        return false;
    }
    if (request.output_path.empty()) {
        if (error_out != nullptr) *error_out = "no output path";
        return false;
    }
    const int serial = ++export_serial_;
    std::string base =
        std::filesystem::path(request.output_path)
            .filename()
            .string();
    QueueJob job;
    job.id = "export-" + std::to_string(serial);
    job.name = base.empty() ? (job.id + "." + request.container)
                          : base;
    job.format = export_form_ != nullptr
                     ? export_form_->FormatSummary()
                     : request.video_codec + " " + request.container;
    job.detail = export_form_ != nullptr
                     ? export_form_->RangeSummary()
                     : (request.range ==
                                ExportRequest::Range::WholeTimeline
                            ? "entire sequence"
                            : "range");
    job.output_path = request.output_path;
    export_requests_[job.id] = request;
    queue_panel_->Enqueue(std::move(job));
    SetStatus("Queued " + job.name);
    return true;
}

void VeApp::ShowProjectSettingsDialog() {
    settings_form_ = ProjectSettingsDialog::Create();
    settings_form_->SetProject(&project_);
    Ui2ProjectSettings s = settings_form_->GetSettings();
    s.display_name = project_name_;
    s.audio_sample_rate = prefs_.audio_sample_rate;
    s.scratch_dir = prefs_.scratch_dir;
    s.transcript_max_minutes = prefs_.transcript_max_minutes;
    s.autosave_enabled = prefs_.autosave_enabled;
    s.autosave_interval_s = prefs_.autosave_interval_s;
    settings_form_->SetSettings(s);
    settings_dialog_ = W::Dialog::Create(
        "Project Settings", settings_form_, "Apply", "Cancel");
    settings_dialog_->SetPanelWidth(480.0f);
    settings_dialog_->SetPanelHeight(430.0f);
    settings_dialog_->SetMovable(true);
    settings_dialog_->SetResizable(true);
    settings_dialog_->SetOnResult([this](W::Dialog::Result r) {
        if (r == W::Dialog::Result::Accepted &&
            settings_form_ != nullptr) {
            if (settings_form_->Validate().empty()) {
                ApplyProjectSettings(settings_form_->GetSettings());
            } else {
                // Reopen on invalid input — the error line is set on
                // the form but the dialog already closed; surface it
                // on the status strip too.
                SetStatus("Settings: " +
                              settings_form_->ErrorText(),
                          true);
            }
        }
    });
    settings_dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(settings_dialog_);
}

void VeApp::ApplyProjectSettings(const Ui2ProjectSettings& s) {
    // Undoable document half — the legacy SetProjectSettingsCommand:
    // snapshot everything the command touches so Revert restores
    // verbatim (canvas/fps + the whole proxy workflow block).
    class SetProjectSettingsCommand final
        : public XER::VideoEditor::Command {
    public:
        explicit SetProjectSettingsCommand(Ui2ProjectSettings next)
            : next_(std::move(next)) {}
        bool Apply(Project& p) override {
            before_.width = p.width;
            before_.height = p.height;
            before_.fps_num = p.fps_num;
            before_.fps_den = p.fps_den;
            before_.use_proxies = p.use_proxies;
            before_.proxy_ingest = p.proxy_ingest;
            before_.proxy_max_dim = p.proxy_max_dim;
            before_.proxy_crf = p.proxy_crf;
            before_.proxy_cache_max_mb = p.proxy_cache_max_mb;
            ApplyTo(p, next_);
            return true;
        }
        bool Revert(Project& p) override {
            ApplyTo(p, before_);
            return true;
        }
        std::string Label() const override {
            return "Project settings";
        }

    private:
        static void ApplyTo(Project& p,
                            const Ui2ProjectSettings& s) {
            p.width = s.width;
            p.height = s.height;
            p.fps_num = s.fps_num;
            p.fps_den = s.fps_den;
            p.use_proxies = s.use_proxies;
            p.proxy_ingest = s.proxy_ingest;
            p.proxy_max_dim = s.proxy_max_dim;
            p.proxy_crf = s.proxy_crf;
            p.proxy_cache_max_mb = s.proxy_cache_max_mb;
        }
        Ui2ProjectSettings before_{};
        Ui2ProjectSettings next_;
    };
    if (history_ != nullptr) {
        history_->Execute(
            std::make_unique<SetProjectSettingsCommand>(s));
        NotifyTimelineChanged();
    }
    // App-level half — straight into prefs (persisted through the
    // normal debounced flush).
    project_name_ = s.display_name;
    prefs_.audio_sample_rate = s.audio_sample_rate;
    prefs_.scratch_dir = s.scratch_dir;
    prefs_.transcript_max_minutes = s.transcript_max_minutes;
    prefs_.autosave_enabled = s.autosave_enabled;
    prefs_.autosave_interval_s = s.autosave_interval_s;
    if (autosave_.IsInitialized()) {
        autosave_.SetIntervalMs(
            static_cast<std::int64_t>(s.autosave_interval_s) * 1000);
    }
    MarkPrefsDirty();
    SyncAutosaveBadge();
    SetStatus("Project settings applied");
}

// ── autosave ────────────────────────────────────────────────────────

void VeApp::InitAutosave() {
    if (project_path_.empty()) return;
    // Premiere-style "autosave" folder next to the project file
    // (the task's <project>.autosave.xerproj lives here as
    // <stem>.autosave-N.xerproj — rotating crash-safe slots).
    const std::string dir = XER::Helper::Path::NormalizeUtf8Path(
        (std::filesystem::path(
             XER::Helper::Path::FromUtf8(project_path_))
             .parent_path() /
         "autosave")
            .string());
    std::string error;
    if (!autosave_.Init(project_path_, dir, &error)) {
        SetStatus("Autosave disabled: " + error, true);
        return;
    }
    autosave_.SetIntervalMs(
        static_cast<std::int64_t>(prefs_.autosave_interval_s) *
        1000);
}

void VeApp::PumpAutosave(double dt_seconds) {
    if (!prefs_.autosave_enabled || !autosave_.IsInitialized()) {
        return;
    }
    autosave_poll_ += dt_seconds;
    if (autosave_poll_ < 2.0) return;   // legacy ~2 s poll cadence
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
    SyncAutosaveBadge();
}

void VeApp::SyncAutosaveBadge() {
    if (status_ == nullptr) return;
    std::vector<StatusStrip::Badge> badges;
    if (autosave_.IsInitialized() && prefs_.autosave_enabled) {
        badges.push_back({"autosave",
                          project_dirty_ ? "autosave • dirty"
                                         : "autosave • saved",
                          project_dirty_});
    } else {
        badges.push_back({"autosave", "autosave off", true});
    }
    if (export_engine_ != nullptr && export_engine_->IsRunning()) {
        badges.push_back({"export", "exporting…", true});
    }
    status_->SetBadges(std::move(badges));
}

void VeApp::CheckAutosaveRecovery() {
    autosave_offer_ = false;
    autosave_offer_dir_.clear();
    autosave_offer_project_.clear();
    if (project_path_.empty() || !autosave_.IsInitialized()) {
        RebuildMenus();
        return;
    }
    const auto slots = autosave_.ListAutosaves();
    if (slots.empty()) {
        RebuildMenus();
        return;
    }
    // Offer only when the newest slot is NEWER than the project file
    // — a slot written before the last manual save is just history.
    std::error_code ec;
    const auto project_time =
        std::filesystem::last_write_time(
            XER::Helper::Path::FromUtf8(project_path_), ec);
    const auto slot_time =
        std::filesystem::last_write_time(
            XER::Helper::Path::FromUtf8(slots.front()), ec);
    if (ec || slot_time <= project_time) {
        RebuildMenus();
        return;
    }
    autosave_offer_ = true;
    autosave_offer_dir_ = autosave_.AutosaveDir();
    autosave_offer_project_ = project_path_;
    RebuildMenus();
    // Restore prompt — same shape as the close guard's dialog.
    auto body = PlaceholderPanel::Create(
        "autosave.offer", "",
        "A newer autosave was found for this project.\n" +
            std::filesystem::path(slots.front())
                .filename()
                .string() +
            "\nRestore it?");
    autosave_dialog_ =
        W::Dialog::Create("Autosave recovery", body,
                          "Restore", "Ignore");
    autosave_dialog_->SetOnResult([this](W::Dialog::Result r) {
        if (r == W::Dialog::Result::Accepted) {
            AcceptAutosaveRecovery();
        } else {
            DeclineAutosaveRecovery();
        }
    });
    autosave_dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(autosave_dialog_);
}

void VeApp::AcceptAutosaveRecovery() {
    if (!autosave_offer_) {
        SetStatus("No autosave to recover", true);
        return;
    }
    autosave_offer_ = false;
    Project restored;
    std::string error;
    if (!XER::VideoEditor::AutosaveManager::LoadLatest(
            autosave_offer_project_, autosave_offer_dir_, &restored,
            &error)) {
        SetStatus("Autosave restore failed: " + error, true);
        RebuildMenus();
        return;
    }
    if (history_ != nullptr) history_->Clear();
    project_ = std::move(restored);
    // Keep the bound path — the autosave only wins its CONTENT back;
    // the next save still lands on the project file.
    playhead_ = 0.0;
    NotifyTimelineChanged();
    project_dirty_ = true;   // recovered content ≠ file on disk
    SyncAutosaveBadge();
    SetStatus("Autosave restored — save to keep it");
    RebuildMenus();
}

void VeApp::DeclineAutosaveRecovery() {
    autosave_offer_ = false;
    SetStatus("Autosave ignored");
    RebuildMenus();
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

// ---- auto face-blur (YuNet) ------------------------------------------
// Mirrors the whisper job's marshalling exactly: the worker runs the
// detector-agnostic frame scan (ai/face_detect.h ScanClipFaces ->
// ScanClipFrames), writes ONLY the mutex-guarded result slot, and
// Tick's PumpFaceBlurJob commits the track + effect through history
// on the UI thread.

void VeApp::StartFaceBlur(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Auto-blur: clip not found", true);
        return;
    }
    if (clip->source.kind != SourceKind::Media ||
        clip->source_path.empty() || clip->is_adjustment) {
        SetStatus("Auto-blur needs a media clip", true);
        return;
    }
    if (face_running_) {
        SetStatus("Face scan already running");
        return;
    }
    const std::string model = Ai::FaceDetectModel();
    if (model.empty()) {
        SetStatus(std::string("Auto-blur: ") +
                      Ai::FaceDetectModelHint(),
                  true);
        return;
    }
    face_cancel_ = false;
    face_progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(face_result_mutex_);
        face_result_ready_ = false;
        face_result_ok_ = false;
        face_result_ = ClipTrack{};
        face_result_error_.clear();
    }
    face_job_clip_id_ = clip_id;
    face_running_ = true;
    SetStatus("Scanning faces on " +
              (clip->name.empty()
                   ? std::filesystem::path(clip->source_path)
                         .filename()
                         .string()
                   : clip->name) +
              "…");
    const Clip scanned = *clip;   // worker owns a copy — never the model
    face_worker_ = std::thread([this, scanned, model] {
        ClipTrack track;
        std::string error;
        const bool ok = Ai::ScanClipFaces(
            scanned, model, Ai::ReframeScanParams{},
            Ai::FaceDetectParams{},
            [this](float f) { face_progress_ = f; },
            [this] { return face_cancel_.load(); }, &track, &error);
        std::lock_guard<std::mutex> lock(face_result_mutex_);
        face_result_ = std::move(track);
        face_result_error_ = error;
        face_result_ok_ = ok && !face_cancel_.load();
        face_result_ready_ = true;
    });
}

void VeApp::PumpFaceBlurJob() {
    if (!face_running_.load()) return;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(face_result_mutex_);
        ready = face_result_ready_;
    }
    if (!ready) return;
    if (face_worker_.joinable()) face_worker_.join();
    face_running_ = false;
    ClipTrack track;
    std::string error;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(face_result_mutex_);
        track = std::move(face_result_);
        error = face_result_error_;
        ok = face_result_ok_;
        face_result_ready_ = false;
    }
    if (!ok) {
        SetStatus("Face scan failed" +
                      (error.empty() ? std::string() : ": " + error),
                  true);
        return;
    }
    const Clip* clip = FindClip(project_, face_job_clip_id_);
    if (clip == nullptr) {
        SetStatus("Face scan finished but the clip is gone", true);
        return;
    }
    Clip updated = *clip;
    updated.object_track = std::move(track);
    // The face-blur preset rides the live track (the compositor
    // resolves the box per frame). Dedup: an existing face_blur
    // keeps its tuned params — a re-scan refreshes the track only.
    const auto already = std::any_of(
        updated.effects.begin(), updated.effects.end(),
        [](const ClipEffect& e) {
            return e.type == "builtin:xer.fx.face_blur" ||
                   e.type == "xer.fx.face_blur";
        });
    if (!already) {
        updated.effects.push_back(
            {"builtin:xer.fx.face_blur", {16.0f, 0.25f, 0.5f}});
    }
    MediaBounds bounds;
    if (const CatalogEntry* e = FindCatalogEntry(clip->source_path)) {
        bounds.duration_seconds = e->duration_seconds;
        bounds.has_video = e->HasVideo();
    } else {
        bounds.duration_seconds = clip->duration;
    }
    std::string err2;
    history_->BeginMacro("Auto-blur faces");
    const bool applied =
        history_->ReplaceClip(face_job_clip_id_, updated, bounds,
                              &err2);
    history_->EndMacro();
    if (!applied) {
        SetStatus("Auto-blur commit failed: " + err2, true);
        return;
    }
    NotifyTimelineChanged();
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Auto-blur faces: %zu track sample(s) + Face Blur",
                  updated.object_track.samples.size());
    SetStatus(buf);
}

// ---- track mask (SAM2.1) ------------------------------------------
// Two-stage like the color pick: "Track Mask (AI)…" (ctx menu) ARMS
// the program monitor's click seam — the next frame click maps into
// source-frame fractions (Ai::CanvasPointToSourceFraction) and
// becomes the SAM2 point prompt for BeginTrackMaskJob. The worker
// runs ScanClipMaskTrack (per-frame re-prompt, ai/sam2_track.h) and
// writes ONLY the mutex-guarded slot; PumpTrackMaskJob commits
// mask_track + builtin:xer.fx.track_mask through history on the UI
// thread — the face-blur marshalling contract verbatim.

void VeApp::StartTrackMask(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Track Mask: clip not found", true);
        return;
    }
    if (clip->source.kind != SourceKind::Media ||
        clip->source_path.empty() || clip->is_adjustment) {
        SetStatus("Track Mask needs a media clip", true);
        return;
    }
    if (!Ai::Sam2ModelsStaged()) {
        SetStatus(std::string("Track Mask: ") + Ai::Sam2ModelHint(),
                  true);
        return;
    }
    if (mask_running_) {
        SetStatus("Mask track already running");
        return;
    }
    // First writer wins on the shared click seam — disarm the color
    // eyedropper if it held it.
    color_pick_armed_ = false;
    track_mask_armed_ = true;
    track_mask_clip_id_ = clip_id;
    if (program_monitor_ != nullptr) {
        program_monitor_->SetClickPointsArmed(true);
    }
    // The prompt applies to the frame under the playhead — seek onto
    // the clip first if the playhead sits outside it (same
    // "prompt at the displayed frame" rule the transform mapping
    // uses).
    if (playhead_ < clip->timeline_start ||
        playhead_ > clip->timeline_start + clip->duration) {
        Seek(clip->timeline_start);
    }
    SetStatus("Track Mask: click the subject in the program monitor");
}

void VeApp::BeginTrackMaskJob(const Clip& clip, float fx, float fy,
                              double src_seconds) {
    if (mask_running_) {
        SetStatus("Mask track already running");
        return;
    }
    const std::string encoder = Ai::Sam2EncoderModel();
    const std::string decoder = Ai::Sam2DecoderModel();
    if (encoder.empty() || decoder.empty()) {
        SetStatus(std::string("Track Mask: ") + Ai::Sam2ModelHint(),
                  true);
        return;
    }
    mask_cancel_ = false;
    mask_progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(mask_result_mutex_);
        mask_result_ready_ = false;
        mask_result_ok_ = false;
        mask_result_ = ClipMaskTrack{};
        mask_result_error_.clear();
    }
    mask_job_clip_id_ = clip.id;
    mask_running_ = true;
    SetStatus("Tracking mask on " +
              (clip.name.empty()
                   ? std::filesystem::path(clip.source_path)
                         .filename()
                         .string()
                   : clip.name) +
              "…");
    const Clip scanned = clip;   // worker owns a copy — never the model
    mask_worker_ = std::thread(
        [this, scanned, encoder, decoder, fx, fy, src_seconds] {
            Ai::Sam2TrackParams params;
            // The armed click is the T0 foreground point.
            params.prompt_points = {fx, fy};
            params.prompt_labels = {1};
            params.start_seconds = src_seconds;
            ClipMaskTrack track;
            std::string error;
            const bool ok = Ai::ScanClipMaskTrack(
                scanned, encoder, decoder, params,
                [this](float f) { mask_progress_ = f; },
                [this] { return mask_cancel_.load(); }, &track,
                &error);
            std::lock_guard<std::mutex> lock(mask_result_mutex_);
            mask_result_ = std::move(track);
            mask_result_error_ = error;
            mask_result_ok_ = ok && !mask_cancel_.load();
            mask_result_ready_ = true;
        });
}

void VeApp::PumpTrackMaskJob() {
    if (!mask_running_.load()) return;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(mask_result_mutex_);
        ready = mask_result_ready_;
    }
    if (!ready) return;
    if (mask_worker_.joinable()) mask_worker_.join();
    mask_running_ = false;
    ClipMaskTrack track;
    std::string error;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mask_result_mutex_);
        track = std::move(mask_result_);
        error = mask_result_error_;
        ok = mask_result_ok_;
        mask_result_ready_ = false;
    }
    if (!ok) {
        SetStatus("Mask track failed" +
                      (error.empty() ? std::string() : ": " + error),
                  true);
        return;
    }
    const Clip* clip = FindClip(project_, mask_job_clip_id_);
    if (clip == nullptr) {
        SetStatus("Mask track finished but the clip is gone", true);
        return;
    }
    Clip updated = *clip;
    updated.mask_track = std::move(track);
    // The track-mask preset rides the live track (the compositor
    // resolves the silhouette per frame). Dedup: an existing
    // track_mask keeps its tuned params — a re-track refreshes the
    // track only.
    const auto already = std::any_of(
        updated.effects.begin(), updated.effects.end(),
        [](const ClipEffect& e) {
            return e.type == "builtin:xer.fx.track_mask" ||
                   e.type == "xer.fx.track_mask";
        });
    if (!already) {
        updated.effects.push_back(
            {"builtin:xer.fx.track_mask", {0.0f, 0.1f, 0.5f}});
    }
    MediaBounds bounds;
    if (const CatalogEntry* e = FindCatalogEntry(clip->source_path)) {
        bounds.duration_seconds = e->duration_seconds;
        bounds.has_video = e->HasVideo();
    } else {
        bounds.duration_seconds = clip->duration;
    }
    std::string err2;
    history_->BeginMacro("Track mask");
    const bool applied =
        history_->ReplaceClip(mask_job_clip_id_, updated, bounds,
                              &err2);
    history_->EndMacro();
    if (!applied) {
        SetStatus("Mask track commit failed: " + err2, true);
        return;
    }
    NotifyTimelineChanged();
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "Track Mask: %zu mask sample(s) + Track Mask fx",
                  updated.mask_track.samples.size());
    SetStatus(buf);
}

void VeApp::ClearMaskTrack(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Clear Mask: clip not found", true);
        return;
    }
    Clip updated = *clip;
    updated.mask_track = ClipMaskTrack{};
    MediaBounds bounds;
    if (const CatalogEntry* e = FindCatalogEntry(clip->source_path)) {
        bounds.duration_seconds = e->duration_seconds;
        bounds.has_video = e->HasVideo();
    } else {
        bounds.duration_seconds = clip->duration;
    }
    std::string err;
    history_->BeginMacro("Clear mask track");
    const bool applied =
        history_->ReplaceClip(clip_id, updated, bounds, &err);
    history_->EndMacro();
    if (!applied) {
        SetStatus("Clear mask failed: " + err, true);
        return;
    }
    NotifyTimelineChanged();
    SetStatus("Mask track cleared");
}

// ---- AI bounces: upscale (bin) + slow-mo (clip) -----------------
// Wave-2 file-producing seam (docs/wiki/research/
// 2026-11-23-ai-features-wave2.md §1/§2): a worker runs
// ai/ai_bounce.h RunVideoBounce — decode -> model -> encode ->
// <stem>.xer.<tag>.mp4 — and writes ONLY the mutex-guarded slot;
// PumpAiBounceJob imports the file into the catalog and (clip
// bounce) repoints the clip at it through history. Both jobs share
// the one slot: they're seconds-per-frame CPU work and
// parallel-bouncing two models would just thrash the same cores.

void VeApp::StartAiUpscale(const std::string& path) {
    if (bounce_running_) {
        SetStatus("An AI bounce is already running");
        return;
    }
    const std::string model = Ai::UpscaleModel();
    if (model.empty()) {
        SetStatus(std::string("AI Upscale: ") +
                      Ai::UpscaleModelHint(),
                  true);
        return;
    }
    const std::string output =
        Ai::AiBounceOutputPath(path, "upscaled");
    bounce_cancel_ = false;
    bounce_progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(bounce_result_mutex_);
        bounce_result_ready_ = false;
        bounce_result_ok_ = false;
        bounce_result_ = Ai::AiBounceResult{};
        bounce_result_error_.clear();
    }
    bounce_job_clip_id_.clear();   // bin bounce — no repoint
    bounce_running_ = true;
    SetStatus("AI upscale 4x running on " +
              std::filesystem::path(path).filename().string() +
              "…");
    bounce_worker_ =
        std::thread([this, path, model, output] {
            Ai::UpscaleX4v3 upscaler;
            std::string error;
            Ai::AiBounceResult result;
            bool ok = upscaler.Load(model, "", &error);
            if (ok) {
                Ai::AiBounceParams params;
                params.source_path = path;
                params.output_path = output;
                params.with_audio = true;
                params.crf = 18;
                auto frame_fn =
                    [&upscaler](const Ai::AiBounceFrame& src,
                                std::vector<std::uint8_t>* out,
                                int* w, int* h,
                                std::string* err) -> bool {
                    return upscaler.UpscaleFrame(
                        src.bgra.data(), src.w, src.h, src.w * 4,
                        Ai::Img2ImgParams{}, nullptr, nullptr, out,
                        w, h, err);
                };
                ok = Ai::RunVideoBounce(
                    params, Ai::MakeFrameTransform(frame_fn),
                    [this](float f) { bounce_progress_ = f; },
                    [this] { return bounce_cancel_.load(); },
                    &result, &error);
            }
            std::lock_guard<std::mutex> lock(bounce_result_mutex_);
            bounce_result_ = std::move(result);
            bounce_result_error_ = std::move(error);
            bounce_result_ok_ = ok && !bounce_cancel_.load();
            bounce_result_ready_ = true;
        });
}

void VeApp::StartAiSlowmo(const std::string& clip_id) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Slow-mo: clip not found", true);
        return;
    }
    if (clip->source.kind != SourceKind::Media ||
        clip->source_path.empty() || clip->is_adjustment) {
        SetStatus("Slow-mo needs a media clip", true);
        return;
    }
    if (clip->playback_rate >= 1.0 - 1e-3) {
        SetStatus("Slow-mo bakes a slowed clip — set Speed below "
                  "100% first");
        return;
    }
    if (bounce_running_) {
        SetStatus("An AI bounce is already running");
        return;
    }
    const std::string model = Ai::RifeModel();
    if (model.empty()) {
        SetStatus(std::string("Smooth slow-mo: ") +
                      Ai::RifeModelHint(),
                  true);
        return;
    }
    if (!Ai::RifeAvailable()) {
        SetStatus("Smooth slow-mo needs the onnxruntime backend "
                  "(RIFE GridSample is unverified on cv::dnn)",
                  true);
        return;
    }
    const double rate = clip->playback_rate;
    const int factor = std::clamp(
        static_cast<int>(std::lround(1.0 / rate)), 2, 8);
    const double in_s = clip->in_point;
    const double out_s = clip->in_point + clip->duration * rate;
    const std::string tag =
        "slowmo" + std::to_string(factor) + "x";
    const std::string output =
        Ai::AiBounceOutputPath(clip->source_path, tag);
    bounce_cancel_ = false;
    bounce_progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(bounce_result_mutex_);
        bounce_result_ready_ = false;
        bounce_result_ok_ = false;
        bounce_result_ = Ai::AiBounceResult{};
        bounce_result_error_.clear();
    }
    bounce_job_clip_id_ = clip_id;
    bounce_job_rate_ = rate;
    bounce_job_factor_ = factor;
    bounce_running_ = true;
    SetStatus("Smooth slow-mo " + std::to_string(factor) +
              "x baking " +
              (clip->name.empty()
                   ? std::filesystem::path(clip->source_path)
                         .filename()
                         .string()
                   : clip->name) +
              "…");
    const std::string source = clip->source_path;
    bounce_worker_ = std::thread(
        [this, source, model, output, in_s, out_s, factor] {
            Ai::RifeInterp rife;
            std::string error;
            Ai::AiBounceResult result;
            // The GridSample gate: ORT only until cv::dnn's warp op
            // is verified (ai/rife_interp.h header note).
            bool ok =
                rife.Load(model, Ai::kRifeRequiredBackend, &error);
            if (ok) {
                Ai::AiBounceParams params;
                params.source_path = source;
                params.output_path = output;
                params.in_seconds = in_s;
                params.out_seconds = out_s;
                params.out_frames_per_in = factor;
                params.with_audio = false;
                params.crf = 18;
                auto interp =
                    [&rife](const Ai::AiBounceFrame& a,
                            const Ai::AiBounceFrame& b, float t,
                            std::vector<std::uint8_t>* out,
                            std::string* err) -> bool {
                    return rife.Interpolate(a.bgra.data(),
                                            b.bgra.data(), a.w,
                                            a.h, a.w * 4, t, out,
                                            err);
                };
                ok = Ai::RunVideoBounce(
                    params,
                    Ai::MakePairInterpTransform(factor, interp),
                    [this](float f) { bounce_progress_ = f; },
                    [this] { return bounce_cancel_.load(); },
                    &result, &error);
            }
            std::lock_guard<std::mutex> lock(bounce_result_mutex_);
            bounce_result_ = std::move(result);
            bounce_result_error_ = std::move(error);
            bounce_result_ok_ = ok && !bounce_cancel_.load();
            bounce_result_ready_ = true;
        });
}

void VeApp::PumpAiBounceJob() {
    if (!bounce_running_.load()) return;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(bounce_result_mutex_);
        ready = bounce_result_ready_;
    }
    if (!ready) {
        // Heartbeat the status line — a bounce is minutes-class
        // work, silence reads as a hang.
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AI bounce running — %.0f%%",
                      std::clamp(bounce_progress_.load() * 100.0f,
                                 0.0f, 100.0f));
        SetStatus(buf);
        return;
    }
    if (bounce_worker_.joinable()) bounce_worker_.join();
    bounce_running_ = false;
    Ai::AiBounceResult result;
    std::string error;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(bounce_result_mutex_);
        result = std::move(bounce_result_);
        error = bounce_result_error_;
        ok = bounce_result_ok_;
        bounce_result_ready_ = false;
    }
    const std::string clip_id = bounce_job_clip_id_;
    bounce_job_clip_id_.clear();
    if (!ok) {
        SetStatus("AI bounce failed" +
                      (error.empty() ? std::string() : ": " + error),
                  true);
        return;
    }
    // Every successful bounce lands in the bin as new media.
    if (catalog_ != nullptr && catalog_->IsOpen()) {
        std::string import_error;
        std::int64_t entry_id = 0;
        if (!catalog_->Import(result.output_path, &entry_id, false,
                              &import_error)) {
            SetStatus("Bounce produced " + result.output_path +
                          " but bin import failed: " + import_error,
                      true);
            return;
        }
        if (media_prep_ != nullptr) {
            media_prep_->RequestImportPoster(result.output_path);
        }
        RefreshBinEntries();
    }
    const std::string out_name =
        std::filesystem::path(result.output_path)
            .filename()
            .string();
    if (clip_id.empty()) {
        SetStatus("AI upscale finished — " + out_name +
                  " added to the bin");
        return;
    }
    // Clip bounce: repoint the clip at the baked file. The bake
    // slowed the consumed span by factor N, so playing the file at
    // rate 1/(rate*N) reproduces the same timeline span — a rate
    // of exactly 1 when the clip's rate was a clean 1/N.
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) {
        SetStatus("Slow-mo bake finished but the clip is gone",
                  true);
        return;
    }
    Clip updated = *clip;
    updated.source_path = result.output_path;
    updated.in_point = 0.0;
    // The bake stretched the consumed span S to S*factor file
    // seconds; the clip's new rate consumes the whole file over its
    // timeline duration: rate' = S*factor/D = rate*factor (== 1
    // exactly when the clip was a clean 1/N slow-down).
    updated.playback_rate =
        bounce_job_rate_ * static_cast<double>(bounce_job_factor_);
    MediaBounds bounds;
    bounds.duration_seconds = result.duration_seconds;
    bounds.has_video = true;
    std::string err2;
    history_->BeginMacro("Smooth slow-mo (AI)");
    const bool applied =
        history_->ReplaceClip(clip_id, updated, bounds, &err2);
    history_->EndMacro();
    if (!applied) {
        SetStatus("Slow-mo commit failed: " + err2, true);
        return;
    }
    NotifyTimelineChanged();
    SetStatus("Smooth slow-mo " +
              std::to_string(bounce_job_factor_) + "x baked — " +
              out_name);
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
