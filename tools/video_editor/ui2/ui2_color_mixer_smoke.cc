// ui2_color_mixer smoke — see ui2_color_mixer_smoke.h. All checks
// run headless: panels are driven through their public verbs and
// real pointer events on the wheel widget; the shell side is a real
// Project + TimelineHistory wired exactly like VeApp's adapters.
#include "ui2_color_mixer_smoke.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "color_panel.h"
#include "mixer_panel.h"
#include "monitor_panel.h"
#include "monitor_surface.h"
#include "scopes_panel.h"
#include "shell_widgets.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "ve_app.h"
#include "ve_theme.h"
#include "widgets/widget.h"
#include "workspace_layout.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;

bool NearlyEqual(float a, float b, float eps = 0.02f) {
    return std::fabs(a - b) <= eps;
}

W::Event Mouse(W::Event::Type type, float x, float y,
               W::MouseButton button = W::MouseButton::Left) {
    W::Event e;
    e.type = type;
    e.position = {x, y};
    e.mouse_button = button;
    return e;
}

Clip MakeClip(const std::string& id) {
    Clip c;
    c.id = id;
    c.source_path = "D:/media/alpha.mp4";
    c.track = 0;
    c.timeline_start = 1.0;
    c.duration = 4.0;
    return c;
}

// ---- 1. ColorPanel wheel hit mapping ----------------------------------

std::string CheckWheelHitMapping() {
    auto panel = ColorPanel::Create();
    const Clip clip = MakeClip("clip_c1");
    panel->SetClipProvider([&clip] { return clip; });
    if (panel->ClipId() != "clip_c1") {
        return "clip provider did not feed the panel";
    }

    ColorParams committed;
    int commits = 0;
    ColorHooks hooks;
    hooks.on_color_changed =
        [&](const std::string& id, const ColorParams& p) {
            committed = p;
            ++commits;
            if (id != "clip_c1") commits += 100;   // wrong id marker
        };
    panel->SetHooks(hooks);

    // Lay the panel out so the wheel widgets get real bounds, then
    // drive a pointer drag on wheel 0 (shadows): press near center,
    // drag right (warm) + up (magenta), release.
    panel->Layout({0.0f, 0.0f, 360.0f, 760.0f});
    W::Widget* wheel = panel->WheelSurface(0);
    if (wheel == nullptr) return "wheel widget missing";
    const Rect wb = wheel->GetBounds();
    if (wb.width < 20.0f || wb.height < 20.0f) {
        return "wheel has no bounds after Layout (" +
               std::to_string(wb.width) + "x" +
               std::to_string(wb.height) + ")";
    }
    const float cx = wb.x + wb.width / 2.0f;
    const float cy = wb.y + wb.height / 2.0f;
    const float r = wb.width / 4.0f;   // inside the disc
    if (!wheel->HandleEvent(Mouse(W::Event::Type::MouseDown, cx, cy))) {
        return "wheel press not consumed";
    }
    wheel->HandleEvent(
        Mouse(W::Event::Type::MouseMove, cx + r, cy - r));
    wheel->HandleEvent(
        Mouse(W::Event::Type::MouseUp, cx + r, cy - r));
    const auto& w0 = panel->Params().wheels[0];
    if (w0.offset_x <= 0.0f || w0.offset_y <= 0.0f) {
        return "wheel drag did not map to offsets (got " +
               std::to_string(w0.offset_x) + "," +
               std::to_string(w0.offset_y) + ")";
    }
    // +r/-r on a disc of radius R normalizes to ~0.5 per axis.
    if (w0.offset_x < 0.3f || w0.offset_x > 0.8f ||
        w0.offset_y < 0.3f || w0.offset_y > 0.8f) {
        return "wheel offset magnitude off (" +
               std::to_string(w0.offset_x) + "," +
               std::to_string(w0.offset_y) + ")";
    }
    if (commits == 0 || committed.wheels[0].offset_x <= 0.0f) {
        return "on_color_changed did not fire with wheel state";
    }
    if (commits >= 100) {
        return "commit carried the wrong clip id";
    }

    // Level verb -> the same payload (midtones).
    panel->SetWheelLevel(1, 0.5f);
    if (!NearlyEqual(committed.wheels[1].level, 0.5f)) {
        return "level commit missing";
    }
    panel->ResetWheel(1);
    if (!NearlyEqual(panel->Params().wheels[1].level, 0.0f)) {
        return "wheel reset did not land";
    }
    return "";
}

// ---- 2. Eyedropper arm + sample round trip -----------------------------

std::string CheckEyedropperRoundTrip() {
    auto panel = ColorPanel::Create();
    const Clip clip = MakeClip("clip_c2");
    panel->SetClipProvider([&clip] { return clip; });

    bool arm_fired = false;
    ColorParams committed;
    int commits = 0;
    ColorHooks hooks;
    hooks.on_arm_pick = [&] { arm_fired = true; };
    hooks.on_color_changed =
        [&](const std::string&, const ColorParams& p) {
            committed = p;
            ++commits;
        };
    panel->SetHooks(hooks);

    panel->ArmWhiteBalance();
    if (!arm_fired) return "on_arm_pick never fired";
    if (!panel->PickArmed()) return "panel did not latch armed state";

    // Sample a warm cast (r>b): the neutralizing correction must
    // push temperature NEGATIVE (cooling) and a slight tint.
    panel->ApplyPickedColor({0.9f, 0.8f, 0.6f, 1.0f});
    if (panel->PickArmed()) {
        return "pick did not disarm after ApplyPickedColor";
    }
    if (commits == 0) {
        return "picked color did not commit";
    }
    if (committed.temperature >= 0.0f) {
        return "warm sample should cool (temp " +
               std::to_string(committed.temperature) + ")";
    }
    // (r-b)=0.3 -> temp = -0.3*255/1.2 ~= -63.75.
    if (!NearlyEqual(committed.temperature, -63.75f, 2.0f)) {
        return "temperature mapping off: " +
               std::to_string(committed.temperature);
    }
    return "";
}

// ---- 3. ScopesPanel pull + synthetic frame analysis --------------------

std::string CheckScopeFrameSource() {
    auto panel = ScopesPanel::Create();
    // Synthetic 64x64 BGRA — left half black, right half white.
    constexpr int W = 64, H = 64;
    auto bgra = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(W) * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = W / 2; x < W; ++x) {
            auto* px = bgra->data() +
                       (static_cast<std::size_t>(y) * W + x) * 4;
            px[0] = px[1] = px[2] = 255;
            px[3] = 255;
        }
    }
    panel->SetFrameSource([bgra] {
        return ScopeFrame{bgra, W, H};
    });
    if (!panel->Refresh()) {
        return "Refresh() reported no new frame analysis";
    }
    if (!panel->AnalysisValid()) {
        return "analysis did not complete";
    }
    const auto& hist = panel->LumaHistogram();
    if (hist[0] == 0 || hist[255] == 0) {
        return "histogram empty at expected extremes";
    }
    if (hist[128] != 0) {
        return "histogram has phantom mid bins";
    }
    const auto& wmax = panel->WaveformMax();
    const auto& wmin = panel->WaveformMin();
    // Left columns (black half): max ~0. Right columns: min ~255.
    if (wmax[16] > 8) {
        return "waveform dark half not dark: " +
               std::to_string(wmax[16]);
    }
    if (wmin[240] < 200) {
        return "waveform bright half not bright: " +
               std::to_string(wmin[240]);
    }
    // Idempotence — same buffer identity -> no re-analysis.
    if (panel->Refresh()) {
        return "Refresh re-analyzed an unchanged frame";
    }
    // Honest stubs stay honest.
    if (ScopesPanel::ModeImplemented(ScopesPanel::Mode::Vectorscope) ||
        ScopesPanel::ModeImplemented(ScopesPanel::Mode::Parade)) {
        return "stub modes claim implementation";
    }
    if (!ScopesPanel::ModeImplemented(ScopesPanel::Mode::Waveform) ||
        !ScopesPanel::ModeImplemented(ScopesPanel::Mode::Histogram)) {
        return "implemented modes misreported";
    }
    return "";
}

// ---- 4. MixerPanel strips + history undo -------------------------------

std::string CheckMixerHistory() {
    Project project;
    Track v1;
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    Track a1;
    a1.kind = TrackKind::Audio;
    a1.name = "A1";
    Track a2;
    a2.kind = TrackKind::Audio;
    a2.name = "A2";
    project.tracks = {v1, a1, a2};
    Clip clip = MakeClip("clip_m1");
    project.clips = {clip};
    TimelineHistory history(project);

    auto panel = MixerPanel::Create();
    panel->SetDocument(&project);
    // 2 audio tracks + master.
    if (panel->StripCount() != 3) {
        return "strip count != audio tracks + master (" +
               std::to_string(panel->StripCount()) + ")";
    }
    if (panel->StripTrack(0) != 1 || panel->StripTrack(1) != 2 ||
        panel->StripTrack(2) != MixerPanel::kMasterBus) {
        return "strip order wrong (tracks then master)";
    }

    MixerHooks hooks;
    hooks.on_track_gain = [&](int track, float db) {
        std::string error;
        history.SetTrackGainDb(track, db, &error);
    };
    hooks.on_track_pan = [&](int track, float pan) {
        std::string error;
        history.SetTrackPan(track, pan, &error);
    };
    hooks.on_track_mute = [&](int track, bool muted) {
        std::string error;
        history.SetTrackMuted(track, muted, &error);
    };
    hooks.on_master_gain = [&](float db) {
        std::string error;
        history.SetMasterGainDb(db, &error);
    };
    panel->SetHooks(hooks);

    // Fader verb -> commit lands on the document.
    panel->ApplyTrackGain(1, -6.0f);
    if (!NearlyEqual(project.tracks[1].gain_db, -6.0f)) {
        return "fader commit did not reach track gain";
    }
    panel->ApplyTrackPan(1, 0.5f);
    if (!NearlyEqual(project.tracks[1].pan, 0.5f)) {
        return "pan commit did not land";
    }
    panel->ApplyTrackMute(2, true);
    if (!project.tracks[2].muted) {
        return "mute commit did not land";
    }
    panel->ApplyMasterGain(-3.0f);
    if (!NearlyEqual(project.master_gain_db, -3.0f)) {
        return "master fader commit did not land";
    }
    // A no-video-track strip must not exist (V1 is skipped).
    if (panel->StripTrack(0) == 0) {
        return "video track produced a strip";
    }

    // Undo round trip — each verb was its own history step.
    if (!history.Undo() ||
        !NearlyEqual(project.master_gain_db, 0.0f)) {
        return "undo did not restore master gain";
    }
    if (!history.Undo() || project.tracks[2].muted) {
        return "undo did not restore mute";
    }
    if (!history.Undo() ||
        !NearlyEqual(project.tracks[1].pan, 0.0f)) {
        return "undo did not restore pan";
    }
    if (!history.Undo() ||
        !NearlyEqual(project.tracks[1].gain_db, 0.0f)) {
        return "undo did not restore track gain";
    }
    return "";
}

// ---- 5. VeApp dock wiring ----------------------------------------------

std::string CheckDockWiring() {
    const std::filesystem::path prefs =
        std::filesystem::temp_directory_path() /
        "xer_ui2_color_mixer_smoke.json";
    VeApp app("", prefs);
    if (app.RightDock() == nullptr || app.Color() == nullptr ||
        app.Mixer() == nullptr || app.Scopes() == nullptr) {
        return "panels not constructed";
    }
    if (app.RightDock()->PanelAt(kRightTabColor).get() !=
        app.Color()) {
        return "Color tab is not the ColorPanel";
    }
    if (app.RightDock()->PanelAt(kRightTabMix).get() !=
        app.Mixer()) {
        return "Mix tab is not the MixerPanel";
    }
    if (app.RightDock()->PanelAt(kRightTabScopes).get() !=
        app.Scopes()) {
        return "Scopes tab is not the ScopesPanel";
    }
    if (!app.Scopes()->HasFrameSource()) {
        return "scopes frame source not wired";
    }
    // Seeded project -> 2 audio strips + master through the real
    // shell wiring.
    if (app.Mixer()->StripCount() != 3) {
        return "mixer did not build strips from the seeded project";
    }
    // Scope chip from the color panel activates the Scopes dock tab.
    ColorPanel* color = app.Color();
    ColorHooks probe;
    // Drive the same path the button drives — the hook is installed
    // on the panel, so fire it directly.
    probe = color->GetHooks();
    if (!probe.on_scope_mode) {
        return "scope-mode hook not wired";
    }
    probe.on_scope_mode(2);   // histogram
    if (app.RightDock()->ActiveIndex() != kRightTabScopes) {
        return "scope chip did not activate the Scopes tab";
    }
    if (app.Scopes()->GetMode() != ScopesPanel::Mode::Histogram) {
        return "scope chip did not set the mode";
    }
    return "";
}

}  // namespace

std::string Ui2ColorMixerSmoke() {
    if (const std::string err = CheckWheelHitMapping(); !err.empty()) {
        return "wheel: " + err;
    }
    if (const std::string err = CheckEyedropperRoundTrip();
        !err.empty()) {
        return "eyedropper: " + err;
    }
    if (const std::string err = CheckScopeFrameSource();
        !err.empty()) {
        return "scopes: " + err;
    }
    if (const std::string err = CheckMixerHistory(); !err.empty()) {
        return "mixer: " + err;
    }
    if (const std::string err = CheckDockWiring(); !err.empty()) {
        return "dock wiring: " + err;
    }
    return "";
}

}  // namespace XER::VideoEditor::Ui2
