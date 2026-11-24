// ui2_shell smoke — logic-only checks on the M1 shell: dock tab
// structure, workspace preset application (collapse + tab + fraction
// state) and the ShortcutRouter's TextInput/KeyDown dispatch through
// a real WidgetTree (focused-widget + overlay guards come free from
// widget_tree.cc's dispatch order).
#include "ui2_shell_smoke.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "monitor_panel.h"
#include "photo_workspace.h"
#include "shell_widgets.h"
#include "timeline_strip.h"
#include "shortcut_router.h"
#include "ve_app.h"
#include "ve_theme.h"
#include "widgets/resizable.h"
#include "widgets/sidebar.h"
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

W::Event KeyUp(W::KeyCode key) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyUp;
    ev.key = key;
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

std::string CheckWorkspaceSwitchGeometry(
    VeApp& app, W::WidgetTree& tree,
    const XER::UI::Theme::Tokens& tokens) {
    const XER::UI::Layout::Rect frame{0.0f, 0.0f, 1440.0f, 860.0f};
    // Real rail-click path — MouseDown + MouseUp on the same hit target
    // synthesizes a Click inside the tree, which bubbles to the
    // Sidebar's on_select_ -> ApplyWorkspaceByName. Driving the events
    // (not the verb) covers the dispatch half of the reported
    // "clicking a preset does nothing" regression.
    const auto click_rail = [&app, &tree](int item) {
        const auto rb = app.NavRail()->GetBounds();
        W::Event down{};
        down.type = W::Event::Type::MouseDown;
        down.mouse_button = W::MouseButton::Left;
        down.position = {rb.x + rb.width * 0.5f,
                         rb.y + 8.0f + item * 34.0f + 17.0f};
        tree.HandleEvent(down);
        W::Event up = down;
        up.type = W::Event::Type::MouseUp;
        tree.HandleEvent(up);
    };
    const auto empty = [](const XER::UI::Layout::Rect& r) {
        return r.width <= 0.0f || r.height <= 0.0f;
    };

    // Editing -> Audio via the rail: the preset hides the left dock —
    // after layout the dock must own NO span (collapsed panels park on
    // an empty rect).
    click_rail(2);   // rail item 2 -> "Audio"
    if (app.CurrentWorkspace() != "Audio") {
        return "rail click did not apply the Audio preset (got '" +
               app.CurrentWorkspace() + "')";
    }
    tree.Layout(frame, tokens);
    if (!app.TopSplit()->IsCollapsed(0) ||
        !empty(app.LeftDock()->GetBounds())) {
        return "Audio preset: left dock still owns span after layout";
    }
    if (empty(app.RightDock()->GetBounds()) ||
        empty(app.MonitorSplit()->GetBounds())) {
        return "Audio preset: right dock/monitors lost their rect";
    }

    // Audio -> Photo: the photo band takes the top split's center; the
    // monitor pair is orphaned and must be parked, docks + timeline
    // collapse.
    click_rail(5);   // rail item 5 -> "Photo"
    if (app.CurrentWorkspace() != "Photo") {
        return "rail click did not apply the Photo preset";
    }
    if (app.PhotoWorkspaceView() == nullptr) {
        return "Photo preset did not create the photo workspace";
    }
    tree.Layout(frame, tokens);
    if (empty(app.PhotoWorkspaceView()->GetBounds())) {
        return "Photo preset: photo workspace got no rect";
    }
    if (!empty(app.MonitorSplit()->GetBounds())) {
        return "Photo preset: monitor split kept its rect (stale "
               "orphan covers the workspace)";
    }
    if (!app.OuterSplit()->IsCollapsed(1) ||
        !empty(app.Timeline()->GetBounds())) {
        return "Photo preset: timeline still owns span";
    }
    // Zero bounds is not enough — a parked panel's children keep
    // their Layout-assigned rects near the origin and paint over the
    // band (the "TIMELINE"/Selection-chip/ruler chrome leaking into
    // the photo top bar). Collapsed/orphaned shell chrome must be
    // INVISIBLE: the only effectively-visible DockTabGroup inside the
    // app is the photo band's own Layers/Adjust dock.
    if (app.Timeline()->IsEffectivelyVisible() ||
        app.MonitorSplit()->IsEffectivelyVisible() ||
        app.LeftDock()->IsEffectivelyVisible() ||
        app.RightDock()->IsEffectivelyVisible()) {
        return "Photo preset: collapsed shell chrome is still "
               "effectively visible (stray paint over the photo band)";
    }
    {
        std::vector<std::string> stray;
        const std::function<void(const W::Widget*)> walk =
            [&](const W::Widget* node) {
                if (node == nullptr ||
                    !node->IsEffectivelyVisible()) {
                    return;
                }
                // The band's own internals (incl. its right-dock tab
                // group) are the allowed answer — don't descend.
                if (node == app.PhotoWorkspaceView()) return;
                if (dynamic_cast<const TimelineStrip*>(node) !=
                    nullptr) {
                    stray.push_back("TimelineStrip");
                }
                if (dynamic_cast<const TimelineSurface*>(node) !=
                    nullptr) {
                    stray.push_back("TimelineSurface");
                }
                if (dynamic_cast<const DockTabGroup*>(node) !=
                    nullptr) {
                    stray.push_back("DockTabGroup");
                }
                for (const auto& child : node->GetChildren()) {
                    walk(child.get());
                }
            };
        for (const auto& child : app.GetChildren()) {
            walk(child.get());
        }
        if (!stray.empty()) {
            std::string names;
            for (const std::string& s : stray) {
                if (!names.empty()) names += ", ";
                names += s;
            }
            return "Photo preset: video chrome still effectively "
                   "visible in the band region: " + names;
        }
    }

    // Photo -> Editing — THE regression: the photo band is orphaned by
    // the panel rebuild; it used to keep its stale full-width rect,
    // paint over the restored docks/monitors and swallow their clicks,
    // leaving the app visually stuck on the Photo workspace.
    click_rail(0);   // rail item 0 -> "Editing"
    if (app.CurrentWorkspace() != "Editing") {
        return "rail click did not apply the Editing preset";
    }
    tree.Layout(frame, tokens);
    if (!empty(app.PhotoWorkspaceView()->GetBounds()) ||
        app.PhotoWorkspaceView()->IsEffectivelyVisible()) {
        return "Photo->Editing: photo workspace kept a stale rect — "
               "it paints over the video layout (stuck workspace)";
    }
    if (empty(app.LeftDock()->GetBounds()) ||
        empty(app.RightDock()->GetBounds()) ||
        empty(app.MonitorSplit()->GetBounds()) ||
        empty(app.Timeline()->GetBounds())) {
        return "Photo->Editing: docks/monitors/timeline did not "
               "reclaim their rects";
    }
    if (app.TopSplit()->IsCollapsed(0) ||
        app.TopSplit()->IsCollapsed(2)) {
        return "Photo->Editing: docks still collapsed";
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
    // Held-key contract: 'k' records a held binding — while it is
    // down, 'j' runs the fixed slow shuttle (not the ladder), and
    // the release routes to the recorded binding's on_up even
    // though the press arrived as TextInput and the release as a
    // KeyUp letter code (cross-channel identity, router's job).
    if (!tree.HandleEvent(Text("k"))) {
        return "'k' not dispatched";
    }
    if (!app.Shortcuts().IsHeld('k')) {
        return "held 'k' press not recorded";
    }
    if (!tree.HandleEvent(Text("j"))) {
        return "held 'j' not dispatched";
    }
    if (app.ProgramPlayback().ShuttleRate() !=
        -VeApp::kHeldShuttleRate) {
        return "K+J did not run the held slow shuttle (rate " +
               std::to_string(app.ProgramPlayback().ShuttleRate()) +
               ")";
    }
    if (app.ProgramMonitor()->VideoSurface().SpeedBadgeRate() !=
        -VeApp::kHeldShuttleRate) {
        return "held shuttle did not push the speed badge";
    }
    if (!tree.HandleEvent(KeyUp(W::KeyCode::J))) {
        return "'j' release did not reach the held binding";
    }
    if (app.ProgramPlayback().ShuttleRate() != 0.0) {
        return "held shuttle did not stop on 'j' release";
    }
    if (!app.Shortcuts().IsHeld('k')) {
        return "'k' hold lost under the 'j' release";
    }
    if (!tree.HandleEvent(KeyUp(W::KeyCode::K))) {
        return "'k' release not routed";
    }
    if (app.Shortcuts().IsHeld('k')) {
        return "'k' still held after its release";
    }
    // Shift+K play-around and Shift+I/O go-to dispatch per scope
    // (program here — the verbs run on the empty project).
    if (!tree.HandleEvent(Text("k", /*shift=*/true))) {
        return "Shift+K not dispatched";
    }
    if (!tree.HandleEvent(Text("i", /*shift=*/true)) ||
        !tree.HandleEvent(Text("o", /*shift=*/true))) {
        return "Shift+I/O not dispatched";
    }
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

std::string CheckFocus(W::WidgetTree& tree) {
    // views FocusManager contract on the kit: traversal focus carries
    // kTraversal (ring visible), and the tree answers which widget owns
    // focus. The VE shell always has focusable widgets (tool strip
    // buttons, fields).
    if (!tree.AdvanceFocus(false)) {
        return "AdvanceFocus found nothing focusable";
    }
    const auto focused = tree.GetFocusedWidget();
    if (focused == nullptr) {
        return "GetFocusedWidget null after AdvanceFocus";
    }
    if (focused->GetFocusReason() != W::FocusReason::kTraversal) {
        return "AdvanceFocus did not tag focus as kTraversal";
    }
    if (!focused->IsFocusRingVisible()) {
        return "traversal focus did not raise the ring";
    }
    tree.RequestFocus(nullptr);
    if (tree.GetFocusedWidget() != nullptr) {
        return "RequestFocus(nullptr) did not clear focus";
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
    if (const std::string err =
            CheckWorkspaceSwitchGeometry(*app, tree, tokens);
        !err.empty()) {
        return "workspace switch: " + err;
    }
    if (const std::string err = CheckRouter(*app, tree); !err.empty()) {
        return "router: " + err;
    }
    if (const std::string err = CheckFocus(tree); !err.empty()) {
        return "focus: " + err;
    }
    if (const std::string err = CheckPrefsRoundTrip(); !err.empty()) {
        return "prefs: " + err;
    }
    tree.SetRoot(nullptr);
    return {};
}

}  // namespace XER::VideoEditor::Ui2
