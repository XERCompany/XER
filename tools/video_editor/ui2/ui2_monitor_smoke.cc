// ui2_monitor smoke — headless checks for the M2 monitor surface +
// chrome. Same style as ui2_shell_smoke: widget state and callback
// assertions through a real WidgetTree, never pixel comparison.
#include "ui2_monitor_smoke.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "monitor_panel.h"
#include "monitor_surface.h"
#include "rendering/renderer.h"
#include "shortcut_router.h"
#include "timeline_bridge.h"
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
    int stroke_calls = 0;
    Layout::Rect last_frame_dst{};
    struct Stroke {
        XER::UI::Theme::Color color;
        float width;
    };
    std::vector<Stroke> strokes;
    std::vector<std::string> texts;

    void BeginFrame(Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(Layout::Rect, XER::UI::Theme::Color,
                  float) override {}
    void StrokeRect(Layout::Rect, XER::UI::Theme::Color color,
                    float width, float) override {
        ++stroke_calls;
        strokes.push_back({color, width});
    }
    bool DrewStroke(XER::UI::Theme::Color color, float width) const {
        for (const Stroke& s : strokes) {
            if (s.width == width && s.color.a == color.a &&
                s.color.r == color.r && s.color.g == color.g &&
                s.color.b == color.b) {
                return true;
            }
        }
        return false;
    }
    void DrawLine(Layout::Point, Layout::Point,
                  XER::UI::Theme::Color color, float) override {
        ++line_calls;
        line_colors.push_back(color);
    }
    int line_calls = 0;
    std::vector<XER::UI::Theme::Color> line_colors;
    bool DrewLine(XER::UI::Theme::Color color) const {
        for (const auto& c : line_colors) {
            if (c.a == color.a && c.r == color.r && c.g == color.g &&
                c.b == color.b) {
                return true;
            }
        }
        return false;
    }
    void DrawText(std::string_view text, Layout::Rect,
                  TextStyle) override {
        ++text_calls;
        texts.emplace_back(text);
    }
    bool DrewText(std::string_view wanted) const {
        return std::find(texts.begin(), texts.end(), wanted) !=
               texts.end();
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
    // WidgetTree::Tick only covers tooltips — the widget subtree
    // (the surface's click clock) ticks via Widget::TickTree, the
    // same contract the shell's frame callback drives.
    surface->TickTree(1.0);   // past the 0.4 s double-click window
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

    // Active-viewer accent border: the scoped panel strokes its
    // border in tokens.accent at 2px instead of the quiet 1px ring.
    if (source->ScopeActive()) return "panel starts scoped";
    source->SetScopeActive(true);
    if (!source->ScopeActive()) return "scope flag did not latch";
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (!r.DrewStroke(tokens.accent, 2.0f)) {
            return "scoped panel did not paint the accent border";
        }
    }
    // Speed badge: a non-1 rate paints the "<rate>x" chip on the
    // surface; 0 / +1 hides it (PreviewBinding pushes the raw rate).
    source->SetSpeedBadge(-0.25);
    if (source->VideoSurface().SpeedBadgeRate() != -0.25) {
        return "speed badge rate did not latch";
    }
    source->VideoSurface().SetDuration(10.0);
    source->VideoSurface().SetFrame(FakeBgra(160, 90), 160, 90);
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (!r.DrewText("-0.25x")) {
            return "speed badge did not paint the rate chip";
        }
    }
    source->SetSpeedBadge(1.0);
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (r.DrewText("-0.25x")) {
            return "badge stayed visible at +1x";
        }
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
    // Last-active-viewer border: the program monitor starts scoped,
    // the source does not (WireMonitorPanels pushes the default).
    if (!app->ProgramMonitor()->ScopeActive() ||
        app->SourceMonitor()->ScopeActive()) {
        return "default scope border is not on the program monitor";
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
    if (!app->SourceMonitor()->ScopeActive() ||
        app->ProgramMonitor()->ScopeActive()) {
        return "source load did not move the scope border";
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
    // F match frame: playhead inside the landed clip loads its media
    // into the source monitor at the matching source time —
    // src_t = in_point + (playhead - start) * rate (the smoke clip
    // is [0,3) at rate 1, in_point 0).
    app->Seek(1.5);
    app->ProgramMatchFrame();
    if (app->SourcePath() != "smoke_src.wav") {
        return "match frame did not reload the clip's source";
    }
    if (!Near(static_cast<float>(
                  app->SourcePlayback().Playhead()),
              1.5f, 0.001f)) {
        return "match frame mapped the wrong source time (got " +
               std::to_string(app->SourcePlayback().Playhead()) +
               ")";
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
    if (!app->ProgramMonitor()->ScopeActive() ||
        app->SourceMonitor()->ScopeActive()) {
        return "program press did not move the scope border back";
    }

    // Transform gizmo end-to-end: a generated ColorMatte clip on the
    // video track (no decoder needed — the live-preview compositor
    // renders it headless) selects via the bridge, the program
    // surface boxes it, a synthetic center drag lands ONE
    // ReplaceClip commit, and a single Undo restores the offset.
    {
        Project gp;
        gp.width = 640;
        gp.height = 360;
        Track gv;
        gv.kind = TrackKind::Video;
        gp.tracks.push_back(gv);
        Clip gclip;
        gclip.id = "gizmo-matte";
        gclip.source.kind = SourceKind::ColorMatte;
        gclip.source.param = "#204060";
        gclip.track = 0;
        gclip.timeline_start = 0.0;
        gclip.duration = 10.0;
        gp.clips.push_back(gclip);
        app->SetProject(gp);
        app->Bridge()->SelectClip("gizmo-matte");
        app->ProgramMonitor()->SetFrame(FakeBgra(640, 360), 640, 360);
        MonitorSurface& gsurf =
            app->ProgramMonitor()->VideoSurface();
        if (!gsurf.GizmoVisible()) {
            return "gizmo did not box the selected matte clip";
        }
        const Layout::Rect gbox = gsurf.GizmoBounds();
        if (gbox.width <= 0.0f || gbox.height <= 0.0f) {
            return "gizmo bounds empty for the matte clip";
        }
        const float gcx = gbox.x + gbox.width * 0.5f;
        const float gcy = gbox.y + gbox.height * 0.5f;
        tree.HandleEvent(MouseDown(gcx, gcy));
        W::Event mv{};
        mv.type = W::Event::Type::MouseMove;
        mv.position = {gcx + 64.0f, gcy};
        tree.HandleEvent(mv);
        tree.HandleEvent(MouseUp(gcx + 64.0f, gcy));
        const Clip* after = FindClip(*app->Document(), "gizmo-matte");
        if (after == nullptr) {
            return "gizmo clip vanished mid-gesture";
        }
        // The matte's source IS the canvas — a +64 client-px move is
        // +64/dest_width normalized offset.
        const float expect =
            64.0f / gsurf.FittedFrameRect().width;
        if (!Near(after->transform.offset_x, expect, 0.01f)) {
            return "gizmo drag did not move the clip (got " +
                   std::to_string(after->transform.offset_x) + ")";
        }
        app->Undo();
        const Clip* undone =
            FindClip(*app->Document(), "gizmo-matte");
        if (undone == nullptr ||
            !Near(undone->transform.offset_x, 0.0f, 0.001f)) {
            return "gizmo drag was not one undo step";
        }
    }

    tree.SetRoot(nullptr);
    return {};
}

// ---- transform gizmo ---------------------------------------------
//
// Surface-level gesture contract: a fake shell binds the gizmo
// members on MonitorHooks and the checks drive real
// press/drag/release sequences through the WidgetTree — box
// geometry, move/scale/rotate math, snap guides, press-to-select,
// double-click reset, Esc cancel, armed-click-points priority.
// Coordinates are canvas-normalized * the pushed frame's fitted
// rect (frame 640x360 == the target canvas dims, so client and
// canvas px coincide modulo the 13 px letterbox).

std::string CheckTransformGizmo() {
    auto surface = MonitorSurface::Create(MonitorSurface::Id::Program);
    W::WidgetTree tree;
    tree.SetRoot(surface);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 640.0f, 400.0f}, tokens);
    surface->SetDuration(10.0);
    surface->SetFrame(FakeBgra(640, 360), 640, 360);
    const Layout::Rect dest = surface->FittedFrameRect();
    if (!Near(dest.width, 640.0f) || !Near(dest.height, 360.0f)) {
        return "unexpected fitted rect for the gizmo check";
    }

    // src 320x180 at {.31,.33,scale .5} -> dest 160x90 with the
    // transform center at canvas (278.4, 163.8) -> client
    // (278.4, 176.8). Box aabb {198.4,131.8,160,90}.
    MonitorGizmoTarget current;
    current.clip_id = "c1";
    current.transform = Transform{0.31f, 0.33f, 0.5f, 0.0f, 1.0f};
    current.src_w = 320;
    current.src_h = 180;
    current.canvas_w = 640;
    current.canvas_h = 360;
    MonitorGizmoTarget c2 = current;
    c2.clip_id = "c2";
    c2.transform = Transform{0.02f, 0.02f, 0.5f, 0.0f, 1.0f};

    int begins = 0, ends = 0, commits = 0, previews = 0,
        selects = 0, resets = 0;
    Transform last_preview{}, last_commit{};
    std::string committed_id, selected_id, reset_id;
    bool snap_on = false;
    int toggled = 0;
    float picked_x = -1.0f;

    MonitorHooks hooks;
    hooks.gizmo_target = [&] { return current; };
    hooks.gizmo_pick = [&](float nx, float ny) -> std::string {
        picked_x = nx;
        // A pick hit only inside a 10% square at the frame's top-left
        // corner — everywhere else reports "no clip" so empty-frame
        // presses keep their classic behavior.
        return (nx < 0.10f && ny < 0.10f) ? c2.clip_id
                                          : std::string{};
    };
    hooks.on_gizmo_select = [&](const std::string& id) {
        ++selects;
        selected_id = id;
        if (id == c2.clip_id) current = c2;   // selection round-trip
    };
    hooks.on_gizmo_begin = [&](const char*) { ++begins; };
    hooks.on_gizmo_end = [&] { ++ends; };
    hooks.on_gizmo_preview =
        [&](const std::string&, const Transform& t) {
            ++previews;
            last_preview = t;
        };
    hooks.on_gizmo_commit =
        [&](const std::string& id, const Transform& t) {
            ++commits;
            committed_id = id;
            last_commit = t;
        };
    hooks.on_gizmo_reset = [&](const std::string& id) {
        ++resets;
        reset_id = id;
    };
    hooks.gizmo_snap_enabled = [&] { return snap_on; };
    hooks.on_toggle_play = [&] { ++toggled; };
    surface->SetHooks(std::move(hooks));

    // ---- box geometry ----
    if (!surface->GizmoVisible()) return "gizmo not visible";
    const Layout::Rect box = surface->GizmoBounds();
    if (!Near(box.x, 198.4f, 0.2f) || !Near(box.y, 131.8f, 0.2f) ||
        !Near(box.width, 160.0f, 0.2f) ||
        !Near(box.height, 90.0f, 0.2f)) {
        return "gizmo bounds mismatch vs the warped rect";
    }
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        if (r.line_calls < 7) {
            return "gizmo did not paint its box/stem/anchor lines";
        }
    }
    const float cx = box.x + box.width * 0.5f;    // 278.4
    const float cy = box.y + box.height * 0.5f;   // 176.8
    const auto mouse_move = [&](float x, float y) {
        W::Event ev{};
        ev.type = W::Event::Type::MouseMove;
        ev.position = {x, y};
        tree.HandleEvent(ev);
    };
    const auto key = [&](W::KeyCode k) {
        W::Event ev{};
        ev.type = W::Event::Type::KeyDown;
        ev.key = k;
        tree.HandleEvent(ev);
    };

    // ---- move drag: +40,+20 client px -> normalized delta ----
    tree.HandleEvent(MouseDown(cx, cy));
    mouse_move(cx + 40.0f, cy + 20.0f);
    if (!surface->GizmoDragging()) return "move drag did not arm";
    tree.HandleEvent(MouseUp(cx + 40.0f, cy + 20.0f));
    if (begins != 1 || ends != 1) {
        return "move drag did not bracket exactly one gesture";
    }
    if (commits != 1 || committed_id != "c1") {
        return "move drag did not commit once";
    }
    if (!Near(last_commit.offset_x, 0.31f + 40.0f / 640.0f, 0.002f) ||
        !Near(last_commit.offset_y, 0.33f + 20.0f / 360.0f, 0.002f)) {
        return "move commit drifted from the pointer delta";
    }
    if (previews == 0) return "move drag produced no previews";
    if (last_commit.scale != 0.5f ||
        last_commit.rotation_deg != 0.0f ||
        last_commit.opacity != 1.0f) {
        return "move drag touched non-position transform fields";
    }

    // ---- click on the box (no drag): no gesture, no toggle ----
    tree.HandleEvent(MouseDown(cx, cy));
    tree.HandleEvent(MouseUp(cx, cy));
    if (begins != 1 || ends != 1) {
        return "plain click on the box opened a gesture";
    }
    if (toggled != 0) return "click inside the box toggled playback";

    // ---- rotate stem: tip sits 22 px above top-edge midpoint ----
    const float tip_x = box.x + box.width * 0.5f;   // top-mid x
    const float tip_y = box.y - 22.0f;
    tree.HandleEvent(MouseDown(tip_x, tip_y));
    mouse_move(cx + 50.0f, cy);   // -90deg start -> 0deg now = +90
    tree.HandleEvent(MouseUp(cx + 50.0f, cy));
    if (commits != 2) return "rotate drag did not commit";
    if (!Near(last_commit.rotation_deg, 90.0f, 1.0f)) {
        return "rotate commit is not +90 degrees";
    }

    // ---- corner scale: SE handle out to 2x radial distance ----
    const float se_x = box.x + box.width;          // 358.4
    const float se_y = box.y + box.height;         // 221.8
    tree.HandleEvent(MouseDown(se_x, se_y));
    mouse_move(cx + 160.0f, cy + 90.0f);           // dist *2
    tree.HandleEvent(MouseUp(cx + 160.0f, cy + 90.0f));
    if (commits != 3) return "corner scale drag did not commit";
    if (!Near(last_commit.scale, 1.0f, 0.02f)) {
        return "corner drag did not double the uniform scale";
    }
    // Anchor (transform center) holds: canvas c=(278.4,163.8) ->
    // offset = c_norm - dw/2 (dw = 320*1.0).
    if (!Near(last_commit.offset_x, 278.4f / 640.0f - 0.25f,
              0.01f) ||
        !Near(last_commit.offset_y, 163.8f / 360.0f - 0.25f,
              0.01f)) {
        return "corner scale moved the anchor point";
    }

    // ---- edge scale: E midpoint — axis ratio into the uniform
    // scale (the model has no per-axis scale) ----
    const float e_x = box.x + box.width;           // right-mid
    tree.HandleEvent(MouseDown(e_x, cy));
    mouse_move(e_x + 80.0f, cy);                   // lx: 80 -> 160
    tree.HandleEvent(MouseUp(e_x + 80.0f, cy));
    if (commits != 4) return "edge scale drag did not commit";
    if (!Near(last_commit.scale, 1.0f, 0.02f)) {
        return "edge drag did not double the uniform scale";
    }

    // ---- Esc cancels: preview(orig) restores, nothing commits ----
    const Transform pre_esc{};
    tree.HandleEvent(MouseDown(cx, cy));
    mouse_move(cx + 30.0f, cy);
    key(W::KeyCode::Escape);
    tree.HandleEvent(MouseUp(cx + 30.0f, cy));
    if (commits != 4) return "Esc-cancelled drag still committed";
    if (ends != begins || ends != 5) {
        return "Esc did not close the gesture macro";
    }
    if (!Near(last_preview.offset_x, current.transform.offset_x) ||
        !Near(last_preview.offset_y, current.transform.offset_y)) {
        return "Esc did not restore the original preview";
    }

    // ---- double-click inside the box resets (hook-out) ----
    tree.HandleEvent(MouseDown(cx, cy));
    tree.HandleEvent(MouseUp(cx, cy));
    tree.HandleEvent(MouseDown(cx, cy));
    tree.HandleEvent(MouseUp(cx, cy));
    if (resets != 1 || reset_id != "c1") {
        return "double-click inside the box did not reset once";
    }

    // ---- snap: center candidate crosses the 0.5 line ----
    snap_on = true;
    // cx cand at +40 px -> .4975, inside the 6 px threshold -> the
    // offset lands so the box center sits exactly on the line.
    tree.HandleEvent(MouseDown(cx, cy));
    mouse_move(cx + 40.0f, cy);
    if (!Near(last_preview.offset_x, 0.375f, 0.002f)) {
        return "move snap did not land the center on the 0.5 line";
    }
    {
        RecordingRenderer r;
        tree.Render(r, tokens);
        // kGizmoSnapColor — the timeline's accent-cyan guide token.
        if (!r.DrewLine({0.13f, 0.83f, 0.93f, 1.0f})) {
            return "snapped drag did not paint the accent guide";
        }
    }
    tree.HandleEvent(MouseUp(cx + 40.0f, cy));
    if (!Near(last_commit.offset_x, 0.375f, 0.002f)) {
        return "snap commit drifted from the snapped offset";
    }
    snap_on = false;

    // ---- press-to-select + promote to move ----
    const int selects_before = selects;
    const float pick_x = dest.x + dest.width * 0.05f;
    const float pick_y = dest.y + dest.height * 0.05f;
    tree.HandleEvent(MouseDown(pick_x, pick_y));
    if (selects != selects_before + 1 || selected_id != "c2") {
        return "press on a clip did not select it";
    }
    mouse_move(pick_x + 20.0f, pick_y);
    tree.HandleEvent(MouseUp(pick_x + 20.0f, pick_y));
    if (committed_id != "c2" || commits != 6) {
        return "post-select drag did not move the picked clip";
    }
    if (!Near(last_commit.offset_x, 0.02f + 20.0f / 640.0f, 0.003f)) {
        return "picked-clip move drifted from the pointer delta";
    }
    current = c2;   // keep the pull consistent with selection

    // ---- empty-frame click still toggles playback ----
    const float empty_x = dest.x + dest.width * 0.6f;
    const float empty_y = dest.y + dest.height * 0.9f;
    tree.HandleEvent(MouseDown(empty_x, empty_y));
    tree.HandleEvent(MouseUp(empty_x, empty_y));
    if (toggled != 1) {
        return "empty-frame click did not toggle playback";
    }

    // ---- armed click points keep priority over the gizmo ----
    surface->SetClickPointsArmed(true);
    float click_nx = -1.0f;
    MonitorHooks& mh = surface->MutableHooks();
    mh.on_click_point = [&](float nx, float) { click_nx = nx; };
    tree.HandleEvent(MouseDown(cx, cy));
    mouse_move(cx + 10.0f, cy);
    tree.HandleEvent(MouseUp(cx + 10.0f, cy));
    if (begins != 7 || commits != 6) {
        return "armed click points did not beat the gizmo";
    }
    surface->SetClickPointsArmed(false);
    (void)picked_x;
    (void)click_nx;
    (void)pre_esc;

    // ---- no selection -> no box ----
    current.clip_id.clear();
    if (surface->GizmoVisible()) {
        return "gizmo stayed visible with an empty target";
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
    if (const std::string err = CheckTransformGizmo(); !err.empty()) {
        return "gizmo: " + err;
    }
    if (const std::string err = CheckShellWiring(); !err.empty()) {
        return "shell: " + err;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui2
