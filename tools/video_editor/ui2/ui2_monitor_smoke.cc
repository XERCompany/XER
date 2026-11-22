// ui2_monitor smoke — headless checks for the M2 monitor surface +
// chrome. Same style as ui2_shell_smoke: widget state and callback
// assertions through a real WidgetTree, never pixel comparison.
#include "ui2_monitor_smoke.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "monitor_panel.h"
#include "monitor_surface.h"
#include "rendering/renderer.h"
#include "shortcut_router.h"
#include "timeline_model.h"
#include "ve_app.h"
#include "ve_theme.h"
#include "widgets/widget_tree.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace W = XER::UI::Widgets;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Tokens;

// Null renderer that records frame draws + text calls — enough to
// assert "the letterboxed frame painted into FittedFrameRect()".
class RecordingRenderer final : public Renderer {
public:
    int frame_calls = 0;
    int text_calls = 0;
    Layout::Rect last_frame_dst{};

    void BeginFrame(Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(Layout::Rect, XER::UI::Theme::Color,
                  float) override {}
    void StrokeRect(Layout::Rect, XER::UI::Theme::Color, float,
                    float) override {}
    void DrawLine(Layout::Point, Layout::Point,
                  XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, Layout::Rect,
                  TextStyle) override {
        ++text_calls;
    }
    Layout::Size MeasureText(std::string_view text,
                             TextStyle) const override {
        return {static_cast<float>(text.size()) * 7.0f, 14.0f};
    }
    bool DrawFrameBgra(const void*, int, int, Layout::Rect dst,
                       float) override {
        ++frame_calls;
        last_frame_dst = dst;
        return true;
    }
};

std::vector<std::uint8_t> FakeBgra(int w, int h) {
    return std::vector<std::uint8_t>(
        static_cast<std::size_t>(w) * h * 4u, 0x80);
}

W::Event MouseDown(float x, float y,
                   W::MouseButton btn = W::MouseButton::Left) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseDown;
    ev.position = {x, y};
    ev.mouse_button = btn;
    return ev;
}

W::Event MouseUp(float x, float y,
                 W::MouseButton btn = W::MouseButton::Left) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseUp;
    ev.position = {x, y};
    ev.mouse_button = btn;
    return ev;
}

W::Event Click(float x, float y,
               W::MouseButton btn = W::MouseButton::Left) {
    W::Event ev{};
    ev.type = W::Event::Type::Click;
    ev.position = {x, y};
    ev.mouse_button = btn;
    return ev;
}

bool Near(float a, float b, float eps = 0.51f) {
    return std::fabs(a - b) <= eps;
}

// ---- surface geometry / overlays --------------------------------

std::string CheckSurfaceGeometry() {
    auto surface = MonitorSurface::Create(MonitorSurface::Id::Program);
    W::WidgetTree tree;
    tree.SetRoot(surface);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 640.0f, 400.0f}, tokens);

    // Empty surface: fitted rect falls back to the video area; the
    // status line paints as text.
    if (surface->HasFrame()) return "surface reports a frame early";
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (r.frame_calls != 0 || r.text_calls == 0) {
            return "empty surface did not paint its status text";
        }
    }

    // 640x360 frame in a 640x(400-ruler) area: width-limited fit,
    // vertically centered (13 px letterbox above/below).
    surface->SetDuration(10.0);
    surface->SetFrame(FakeBgra(640, 360), 640, 360);
    if (!surface->HasFrame()) return "frame rejected";
    const Layout::Rect fit = surface->FittedFrameRect();
    const Layout::Rect area = surface->VideoAreaRect();
    if (!Near(fit.width, 640.0f) || !Near(fit.height, 360.0f)) {
        return "fit rect is not width-limited 16:9";
    }
    if (!Near(fit.y, area.y + (area.height - 360.0f) * 0.5f)) {
        return "fit rect not centered in the video area";
    }
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (r.frame_calls != 1) return "frame did not paint";
        if (!Near(r.last_frame_dst.x, fit.x) ||
            !Near(r.last_frame_dst.y, fit.y) ||
            !Near(r.last_frame_dst.width, fit.width) ||
            !Near(r.last_frame_dst.height, fit.height)) {
            return "frame painted outside the fitted rect";
        }
    }

    // Hit mapping: the fitted-frame center maps to (0.5, 0.5) and a
    // point outside the frame misses.
    float nx = -1.0f, ny = -1.0f;
    if (!surface->FrameNormPoint(
            {fit.x + fit.width * 0.5f, fit.y + fit.height * 0.5f},
            &nx, &ny) ||
        !Near(nx, 0.5f, 1e-3f) || !Near(ny, 0.5f, 1e-3f)) {
        return "frame center did not map to (0.5,0.5)";
    }
    if (surface->FrameNormPoint({fit.x - 4.0f, fit.y}, &nx, &ny)) {
        return "point outside the frame mapped anyway";
    }

    // Ruler: mid-strip maps to the duration midpoint; the strip is
    // the bottom kRulerHeight px.
    const Layout::Rect ruler = surface->RulerRect();
    if (ruler.height <= 0.0f || ruler.y + ruler.height > 400.0f) {
        return "ruler rect outside the surface";
    }
    if (!Near(static_cast<float>(
                  surface->RulerTimeAtX(ruler.x + ruler.width / 2)),
              5.0f, 0.01f)) {
        return "ruler midpoint is not duration/2";
    }

    // Zoom: 2x doubles the fitted rect; SetViewCenter pans; fit
    // resets both.
    surface->SetZoom(2.0f);
    const Layout::Rect zoomed = surface->FittedFrameRect();
    if (!Near(zoomed.width, fit.width * 2.0f) ||
        !Near(zoomed.height, fit.height * 2.0f)) {
        return "zoom did not scale the fitted rect";
    }
    surface->SetViewCenter(0.75f, 0.25f);
    const auto [cx, cy] = surface->ViewCenter();
    if (!Near(cx, 0.75f) || !Near(cy, 0.25f)) {
        return "view center did not take";
    }
    surface->ZoomFit();
    if (surface->Zoomed()) return "ZoomFit did not reset";

    tree.SetRoot(nullptr);
    return {};
}

std::string CheckSurfaceTogglesAndHooks() {
    auto surface = MonitorSurface::Create(MonitorSurface::Id::Source);
    W::WidgetTree tree;
    tree.SetRoot(surface);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 640.0f, 400.0f}, tokens);
    surface->SetDuration(10.0);
    surface->SetFrame(FakeBgra(320, 180), 320, 180);

    surface->SetSafeMargins(true);
    surface->SetGrid(true);
    surface->SetLoop(true);
    surface->SetLoopRange(2.0, 4.0);
    surface->SetWorkArea(1.0, 8.0);
    surface->SetMarks(2.0, 6.0);
    surface->SetMarkers({Marker{1.0, "a", {}}, Marker{5.0, "b", {}}});
    surface->SetMarkPoints({{160, 90}});
    if (!surface->SafeMargins() || !surface->Grid() ||
        !surface->Loop()) {
        return "overlay toggles did not latch";
    }
    const auto [li, lo] = surface->LoopRange();
    const auto [mi, mo] = surface->Marks();
    if (li != 2.0 || lo != 4.0 || mi != 2.0 || mo != 6.0) {
        return "loop/marks round-trip drifted";
    }
    // Overlays ride the same frame draw path — one frame call, no
    // throw, status text gone now that pixels exist.
    RecordingRenderer r;
    tree.Render(r, tokens);
    if (r.frame_calls != 1) return "overlay paint dropped the frame";

    // Hook dispatch through the real tree: press activates, a ruler
    // click seeks, a frame click toggles play, an armed click
    // reports frame-normalized coords.
    int activated = 0, toggled = 0;
    double seeked = -1.0;
    float click_x = -1.0f, click_y = -1.0f;
    MonitorHooks hooks;
    hooks.on_activate = [&] { ++activated; };
    hooks.on_toggle_play = [&] { ++toggled; };
    hooks.on_time_changed = [&] (double t) { seeked = t; };
    hooks.on_click_point = [&] (float x, float y) {
        click_x = x;
        click_y = y;
    };
    surface->SetHooks(std::move(hooks));

    const Layout::Rect ruler = surface->RulerRect();
    tree.HandleEvent(MouseDown(ruler.x + ruler.width * 0.25f,
                               ruler.y + 2.0f));
    if (activated == 0) return "press did not fire on_activate";
    if (seeked < 0.0) return "ruler press did not seek";
    if (!Near(static_cast<float>(seeked), 2.5f, 0.05f)) {
        return "ruler seek mapped the wrong time";
    }
    tree.HandleEvent(MouseUp(ruler.x + ruler.width * 0.25f,
                             ruler.y + 2.0f));

    const Layout::Rect fit = surface->FittedFrameRect();
    tree.HandleEvent(
        Click(fit.x + fit.width * 0.5f, fit.y + fit.height * 0.5f));
    if (toggled == 0) return "frame click did not toggle play";

    surface->SetClickPointsArmed(true);
    tree.Tick(1.0);   // past the 0.4 s double-click window
    tree.HandleEvent(
        Click(fit.x + fit.width * 0.25f, fit.y + fit.height * 0.5f));
    if (click_x < 0.0f) return "armed click did not report a point";
    if (!Near(click_x, 0.25f, 0.01f) || !Near(click_y, 0.5f, 0.01f)) {
        return "click point not frame-normalized";
    }
    // Armed clicks report the point INSTEAD of toggling playback —
    // one toggle total (the unarmed click above).
    if (toggled != 1) return "armed click toggled play";

    tree.SetRoot(nullptr);
    return {};
}

// ---- panel chrome ------------------------------------------------

std::string CheckPanelChrome() {
    const auto tokens = Theme::DarkTokens();

    auto program = MonitorPanel::Create(MonitorPanel::Kind::Program);
    auto source = MonitorPanel::Create(MonitorPanel::Kind::Source);
    if (program->Chrome(MonitorPanel::ChromeId::Insert) != nullptr ||
        program->Chrome(MonitorPanel::ChromeId::Lift) == nullptr) {
        return "program panel carries the source edit cluster";
    }
    if (source->Chrome(MonitorPanel::ChromeId::Insert) == nullptr ||
        source->Chrome(MonitorPanel::ChromeId::Lift) != nullptr) {
        return "source panel carries the program edit cluster";
    }

    // Layout + pushed state round-trips through the real tree.
    W::WidgetTree tree;
    tree.SetRoot(source);
    tree.Layout({0.0f, 0.0f, 720.0f, 480.0f}, tokens);
    source->SetFps(24, 1);
    source->SetTimecode(2.5);
    source->SetDuration(10.0);
    source->SetQualityScale(0.5f);
    source->SetPeakLevels(0.5f, 0.25f);
    source->SetProxyBadge("PRX");
    source->SetPlaying(true);
    if (!source->IsPlaying()) return "playing state did not latch";
    if (source->QualityScale() < 0.49f ||
        source->QualityScale() > 0.51f) {
        return "quality scale drifted";
    }
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (r.text_calls == 0) return "panel painted no chrome text";
    }

    // Hook dispatch: press inside the panel activates; a click in
    // the frame toggles play.
    int activated = 0, toggled = 0, marked_in = 0;
    MonitorPanel::Hooks hooks;
    hooks.on_activate = [&] { ++activated; };
    hooks.on_play_toggle = [&] { ++toggled; };
    hooks.on_mark_in = [&] { ++marked_in; };
    source->SetHooks(std::move(hooks));
    const Layout::Rect fit = source->VideoSurface().FittedFrameRect();
    tree.HandleEvent(MouseDown(fit.x + 4.0f, fit.y + 4.0f));
    if (activated == 0) return "panel press did not activate";
    tree.HandleEvent(MouseUp(fit.x + 4.0f, fit.y + 4.0f));
    tree.HandleEvent(
        Click(fit.x + fit.width * 0.5f, fit.y + fit.height * 0.5f));
    if (toggled == 0) return "panel frame click did not toggle";

    tree.SetRoot(nullptr);
    return {};
}

// ---- shell wiring ------------------------------------------------

std::string CheckShellWiring() {
    const auto prefs_path =
        std::filesystem::temp_directory_path() /
        "xer_ui2_monitor_smoke_prefs" / "ui2_shell.json";
    std::error_code ec;
    std::filesystem::remove_all(prefs_path.parent_path(), ec);

    auto app = std::make_shared<VeApp>(std::string{}, prefs_path);
    W::WidgetTree tree;
    tree.SetRoot(app);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 1440.0f, 860.0f}, tokens);
    app->InstallShortcutRouter();

    if (app->ProgramMonitor() == nullptr ||
        app->SourceMonitor() == nullptr) {
        return "monitor panels missing";
    }
    if (app->Shortcuts().ActiveScope() !=
        std::string(ShortcutRouter::kScopeProgram)) {
        return "default scope is not program";
    }

    // Program transport — real binding, empty project (zero clips
    // decode nothing; the engine worker is bounded by the app dtor).
    app->ProgramTogglePlay();
    if (!app->ProgramMonitor()->IsPlaying()) {
        return "program play did not reach the panel";
    }
    app->ProgramShuttleStop();
    if (app->ProgramMonitor()->IsPlaying()) {
        return "program stop did not reach the panel";
    }

    // Source load (metadata only — the synthetic project decodes
    // nothing until a transport action, and the smoke never plays
    // the source engine to the first frame).
    app->OpenInSourceMonitor("smoke_src.wav", "smoke_src.wav", 4.0,
                             30, 1, /*has_video=*/false,
                             /*has_audio=*/true, 0, 0);
    if (!app->SourceLoaded()) return "source load did not take";
    if (app->Shortcuts().ActiveScope() !=
        std::string(ShortcutRouter::kScopeSource)) {
        return "source load did not activate the source scope";
    }

    // Marks at the playhead -> insert edit lands a clip on A1
    // through history (undoable, three-point dispatch).
    const std::size_t clips_before = app->Document()->clips.size();
    app->SourceMark(true);   // in = 0 (playhead parked at 0)
    app->SourceSeek(3.0);
    app->SourceMark(false);  // out = 3
    app->EditSourceToTimeline(/*insert=*/false);
    if (app->Document()->clips.size() != clips_before + 1) {
        return "source overwrite edit did not land a clip";
    }
    app->Undo();
    if (app->Document()->clips.size() != clips_before) {
        return "source edit did not undo";
    }

    // Exclusive playback: program play stops the source transport.
    app->SourceTogglePlay();
    if (!app->SourceMonitor()->IsPlaying()) {
        return "source play did not reach the panel";
    }
    app->ProgramTogglePlay();
    if (app->SourceMonitor()->IsPlaying()) {
        return "program play did not stop the source (exclusive)";
    }
    if (!app->ProgramMonitor()->IsPlaying()) {
        return "program play did not start";
    }
    app->ProgramShuttleStop();

    // Ruler scrub routes through the surface hook -> bridge seek ->
    // both the doc playhead and the binding.
    const Layout::Rect ruler =
        app->ProgramMonitor()->VideoSurface().RulerRect();
    tree.HandleEvent(
        MouseDown(ruler.x + ruler.width * 0.5f, ruler.y + 2.0f));
    tree.HandleEvent(
        MouseUp(ruler.x + ruler.width * 0.5f, ruler.y + 2.0f));
    // Empty project has duration 0 — the scrub clamps to 0; the
    // assertion is that the scope switched to program.
    if (app->Shortcuts().ActiveScope() !=
        std::string(ShortcutRouter::kScopeProgram)) {
        return "program press did not activate the program scope";
    }

    tree.SetRoot(nullptr);
    return {};
}

}  // namespace

std::string Ui2MonitorSmoke() {
    if (const std::string err = CheckSurfaceGeometry(); !err.empty()) {
        return "geometry: " + err;
    }
    if (const std::string err = CheckSurfaceTogglesAndHooks();
        !err.empty()) {
        return "toggles/hooks: " + err;
    }
    if (const std::string err = CheckPanelChrome(); !err.empty()) {
        return "panel: " + err;
    }
    if (const std::string err = CheckShellWiring(); !err.empty()) {
        return "shell: " + err;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui2
