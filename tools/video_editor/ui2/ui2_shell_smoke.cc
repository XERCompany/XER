// ui2_shell smoke — logic-only checks on the M1 shell: dock tab
// structure, workspace preset application (collapse + tab + fraction
// state) and the ShortcutRouter's TextInput/KeyDown dispatch through
// a real WidgetTree (focused-widget + overlay guards come free from
// widget_tree.cc's dispatch order).
#include "ui2_shell_smoke.h"

#include <filesystem>
#include <memory>
#include <string>

#include "monitor_panel.h"
#include "shell_widgets.h"
#include "timeline_strip.h"
#include "shortcut_router.h"
#include "ve_app.h"
#include "ve_theme.h"
#include "widgets/widget_tree.h"
#include "workspace_layout.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;

W::Event Text(const char* text, bool shift = false, bool alt = false) {
    W::Event ev{};
    ev.type = W::Event::Type::TextInput;
    ev.text = text;
    ev.is_shift = shift;
    ev.is_alt = alt;
    return ev;
}

W::Event Key(W::KeyCode key, bool ctrl = false) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    ev.is_ctrl = ctrl;
    return ev;
}

std::string CheckStructure(VeApp& app) {
    if (app.TitleBar() == nullptr) return "no title bar";
    if (app.NavRail() == nullptr) return "no nav rail";
    if (app.LeftDock() == nullptr || app.RightDock() == nullptr) {
        return "missing dock groups";
    }
    if (app.LeftDock()->TabCount() != kLeftTabCount) {
        return "left dock tab count != 5";
    }
    if (app.RightDock()->TabCount() != kRightTabCount) {
        return "right dock tab count != 9";
    }
    if (app.TopSplit() == nullptr || app.OuterSplit() == nullptr ||
        app.MonitorSplit() == nullptr) {
        return "missing resizable splits";
    }
    if (app.Timeline() == nullptr || app.Status() == nullptr) {
        return "missing timeline/status strips";
    }
    return {};
}

std::string CheckWorkspaces(VeApp& app) {
    if (!app.ApplyWorkspaceByName("Audio")) {
        return "Audio preset missing";
    }
    if (app.LeftDockVisible()) {
        return "Audio preset should hide the left dock";
    }
    if (app.RightDock()->ActiveIndex() != kRightTabMix) {
        return "Audio preset should activate the Mix tab";
    }
    if (!app.ApplyWorkspaceByName("Assembly")) {
        return "Assembly preset missing";
    }
    if (!app.LeftDockVisible() || app.RightDockVisible()) {
        return "Assembly preset visibility wrong";
    }
    if (app.TimelineFraction() < 0.4) {
        return "Assembly preset should raise the timeline share";
    }
    if (!app.ApplyWorkspaceByName("Editing")) {
        return "Editing preset missing";
    }
    if (!app.LeftDockVisible() || !app.RightDockVisible() ||
        app.RightDock()->ActiveIndex() != kRightTabInspector) {
        return "Editing preset restore failed";
    }
    if (app.ApplyWorkspaceByName("NoSuchWorkspace")) {
        return "unknown preset name accepted";
    }
    return {};
}

std::string CheckRouter(VeApp& app, W::WidgetTree& tree) {
    // 'j' (TextInput — unmodified letters never produce KeyDown) hits
    // the program-scope binding by default: the M2 binding runs a
    // real shuttle step on the empty project (no clips decode
    // anything) and latches the panel's playing state.
    app.ProgramPlayback().SetAutocache(false);
    if (!tree.HandleEvent(Text("j"))) {
        return "'j' not dispatched";
    }
    if (app.Status()->StatusText().find("Shuttle") ==
        std::string::npos) {
        return "'j' did not reach the program shuttle binding";
    }
    if (!app.ProgramMonitor()->IsPlaying()) {
        return "program shuttle did not reach the panel";
    }
    app.ProgramShuttleStop();
    // Last-active-viewer routing: switch to the source scope — the
    // same key dispatches to the source binding (a no-op until a
    // clip loads; ui2_monitor covers the real source transport).
    app.Shortcuts().ActivateScope(ShortcutRouter::kScopeSource);
    if (app.Shortcuts().ActiveScope() !=
        std::string(ShortcutRouter::kScopeSource)) {
        return "ActivateScope did not switch to source";
    }
    if (!tree.HandleEvent(Text("j"))) {
        return "'j' not dispatched in source scope";
    }
    app.Shortcuts().ActivateScope(ShortcutRouter::kScopeProgram);
    // Tool letters bind globally.
    if (!tree.HandleEvent(Text("c"))) {
        return "'c' razor tool not dispatched";
    }
    if (app.Timeline()->ActiveTool() != 1) {
        return "'c' did not select the razor tool";
    }
    // Ctrl chords go through KeyDown.
    if (!tree.HandleEvent(Key(W::KeyCode::S, /*ctrl=*/true))) {
        return "Ctrl+S not dispatched";
    }
    if (!app.Shortcuts().IsBound(Key(W::KeyCode::Space)) ||
        !app.Shortcuts().IsBound(Text("v"))) {
        return "IsBound misses registered bindings";
    }
    // Multi-byte IME text is never a shortcut.
    W::Event ime{};
    ime.type = W::Event::Type::TextInput;
    ime.text = "\xE6\x97\xA5";  // multi-byte UTF-8 (e.g. CJK char)
    if (tree.HandleEvent(ime)) {
        return "multi-byte text consumed as a shortcut";
    }
    return {};
}

std::string CheckPrefsRoundTrip() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui2_shell_smoke";
    const auto path = dir / "ui2_shell.json";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ShellPrefs out;
    out.workspace = "Color";
    out.right_tab = kRightTabColor;
    out.top_sizes = {0.2f, 0.55f, 0.25f};
    std::string error;
    if (!SaveShellPrefs(path, out, &error)) {
        return "save failed: " + error;
    }
    ShellPrefs back;
    if (!LoadShellPrefs(path, &back, &error)) {
        return "load failed: " + error;
    }
    if (back.workspace != "Color" || back.right_tab != kRightTabColor ||
        back.top_sizes.size() != 3u) {
        return "round trip drifted";
    }
    std::filesystem::remove_all(dir, ec);
    return {};
}

}  // namespace

std::string Ui2ShellSmoke() {
    const auto prefs_path =
        std::filesystem::temp_directory_path() /
        "xer_ui2_shell_smoke_prefs" / "ui2_shell.json";
    std::error_code ec;
    std::filesystem::remove_all(prefs_path.parent_path(), ec);

    auto app = std::make_shared<VeApp>(std::string{}, prefs_path);
    W::WidgetTree tree;
    tree.SetRoot(app);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 1440.0f, 860.0f}, tokens);
    app->InstallShortcutRouter();

    if (const std::string err = CheckStructure(*app); !err.empty()) {
        return "structure: " + err;
    }
    if (const std::string err = CheckWorkspaces(*app); !err.empty()) {
        return "workspaces: " + err;
    }
    if (const std::string err = CheckRouter(*app, tree); !err.empty()) {
        return "router: " + err;
    }
    if (const std::string err = CheckPrefsRoundTrip(); !err.empty()) {
        return "prefs: " + err;
    }
    tree.SetRoot(nullptr);
    return {};
}

}  // namespace XER::VideoEditor::Ui2
