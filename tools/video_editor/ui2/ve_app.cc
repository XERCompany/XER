#include "ve_app.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "overlays/toast.h"
#include "rendering/renderer.h"
#include "shell_widgets.h"
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
    BuildShell();
    BindShellShortcuts();
    ApplyPrefsToShell();
    if (const WorkspaceLayout* ws = FindWorkspace(prefs_.workspace)) {
        ApplyWorkspace(*ws);   // preset wins over raw fields
    }
    SetStatus("ui2 shell — M1 placeholder build");
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

    // Monitor region: Resizable Source|Program pair (placeholder
    // surfaces; the tabbed single-viewer variant swaps this child in
    // when monitors_tabbed_ lands with the real MonitorSurface).
    source_monitor_ = PH("monitor.source", "Source Monitor",
                         "PreviewEngine source feed (M2)");
    program_monitor_ = PH("monitor.program", "Program Monitor",
                          "Composited program feed (M2)");
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
    timeline_->SetOnToolChanged(
        [](int) { /* tool change → status via router path */ });
    timeline_->SetOnSnapToggled([this](bool enabled) {
        SetStatus(enabled ? "Snap enabled" : "Snap disabled");
    });

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

void VeApp::SetStatus(const std::string& text) {
    if (status_ != nullptr) status_->SetStatusText(text);
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
    // MonitorHooks::on_activate (last-active-viewer rule).
    shortcuts_.BindKey('j', status("Source shuttle reverse (M2)"),
                       R::kScopeSource);
    shortcuts_.BindKey('k', status("Source shuttle stop (M2)"),
                       R::kScopeSource);
    shortcuts_.BindKey('l', status("Source shuttle forward (M2)"),
                       R::kScopeSource);
    shortcuts_.BindKey('j', status("Shuttle reverse (M2)"),
                       R::kScopeProgram);
    shortcuts_.BindKey('k', status("Shuttle stop (M2)"),
                       R::kScopeProgram);
    shortcuts_.BindKey('l', status("Shuttle forward (M2)"),
                       R::kScopeProgram);
    shortcuts_.BindKey('i', status("Mark in (M2)"), R::kScopeSource);
    shortcuts_.BindKey('o', status("Mark out (M2)"), R::kScopeSource);
    shortcuts_.BindAltKey('i', status("Clear mark in (M2)"),
                          R::kScopeSource);
    shortcuts_.BindAltKey('o', status("Clear mark out (M2)"),
                          R::kScopeSource);
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
    shortcuts_.BindKey('m', status("Add marker (M2)"));
    shortcuts_.BindShiftKey('m', status("Next marker (M2)"));
    shortcuts_.BindKey('s', [this] {
        timeline_->SetSnapEnabled(!timeline_->SnapEnabled());
        if (timeline_->SnapEnabled()) {
            SetStatus("Snap enabled");
        } else {
            SetStatus("Snap disabled");
        }
        return true;
    });
    shortcuts_.BindChord(W::KeyCode::Space, status("Toggle play (M2)"));
    shortcuts_.BindChord(W::KeyCode::ArrowLeft,
                         status("Step −1 frame (M2)"));
    shortcuts_.BindChord(W::KeyCode::ArrowRight,
                         status("Step +1 frame (M2)"));
    shortcuts_.BindChord(W::KeyCode::Home, status("Go to start (M2)"));
    shortcuts_.BindChord(W::KeyCode::End, status("Go to end (M2)"));
    shortcuts_.BindChord(W::KeyCode::PageDown,
                         status("Next marker (M2)"));
    shortcuts_.BindChord(W::KeyCode::PageUp,
                         status("Previous marker (M2)"));
    shortcuts_.BindChord(W::KeyCode::Delete,
                         status("Delete selection (M2)"));
    shortcuts_.BindChord(W::KeyCode::F5,
                         status("Play from playhead (M2)"));
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
    shortcuts_.BindChord(W::KeyCode::Z, status("Undo (M2)"),
                         /*ctrl=*/true);
    shortcuts_.BindChord(W::KeyCode::Y, status("Redo (M2)"),
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
