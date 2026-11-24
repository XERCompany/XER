// ui2_timeline smoke — headless geometry/event checks on the M2
// TimelineSurface + TimelineStrip + TimelineBridge. No window, no
// renderer: geometry helpers and HandleEvent are exercised directly
// with synthetic Widget::Event values, edits are recorded through a
// scripted TimelineHooks bundle.
#include "ui2_timeline_smoke.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "timeline_bridge.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "timeline_strip.h"
#include "timeline_surface.h"
#include "timeline_tools.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;

// ---- scripted hook recorder ----------------------------------------

struct Rec {
    int selects = 0;
    std::string last_select;
    int seeks = 0;
    double last_seek = -1.0;
    int moves = 0;
    std::string move_id;
    double move_start = 0.0;
    int trims = 0;
    int splits = 0;
    std::string split_id;
    double split_at = 0.0;
    int gesture_begins = 0;
    int gesture_ends = 0;
    int markers = 0;
    double last_marker = 0.0;
    int work_areas = 0;
    double work_in = 0.0, work_out = 0.0;
    int locks = 0, mutes = 0, hides = 0;
    bool lock_on = false;
    int statuses = 0;
    int peaks_calls = 0;
    int poster_calls = 0;
    int bounds_calls = 0;
};

TimelineHooks RecordingHooks(Rec& r) {
    TimelineHooks h;
    h.on_select_clip = [&r](const std::string& id) {
        ++r.selects;
        r.last_select = id;
    };
    h.on_seek = [&r](double t) {
        ++r.seeks;
        r.last_seek = t;
    };
    h.on_move_clip = [&r](const std::string& id, double start) {
        ++r.moves;
        r.move_id = id;
        r.move_start = start;
    };
    h.on_trim_clip = [&r](const std::string&, TrimEdge, double) {
        ++r.trims;
    };
    h.on_split_clip = [&r](const std::string& id, double t) {
        ++r.splits;
        r.split_id = id;
        r.split_at = t;
    };
    h.on_gesture_begin = [&r](const char*) { ++r.gesture_begins; };
    h.on_gesture_end = [&r] { ++r.gesture_ends; };
    h.on_add_marker = [&r](double t, const std::string&) {
        ++r.markers;
        r.last_marker = t;
    };
    h.on_set_work_area = [&r](double in, double out) {
        ++r.work_areas;
        r.work_in = in;
        r.work_out = out;
    };
    h.on_track_locked = [&r](int, bool on) {
        ++r.locks;
        r.lock_on = on;
    };
    h.on_track_mute = [&r](int, bool) { ++r.mutes; };
    h.on_track_hidden = [&r](int, bool) { ++r.hides; };
    h.on_status = [&r](const std::string&, bool) { ++r.statuses; };
    // Async seams answer kPending — the lookup must return at once
    // (never block paint on decode).
    h.get_audio_peaks = [&r](const std::string&, AudioPeaks*) {
        ++r.peaks_calls;
        return MediaLookup::kPending;
    };
    h.get_poster =
        [&r](const std::string&, double, video::Frame*) {
            ++r.poster_calls;
            return MediaLookup::kPending;
        };
    h.get_media_bounds =
        [&r](const std::string&, MediaBounds* out) {
            ++r.bounds_calls;
            out->duration_seconds = 12.0;
            out->has_video = true;
            return true;
        };
    return h;
}

// ---- fixture --------------------------------------------------------

Project MakeProject() {
    Project p;
    Track v1; v1.kind = TrackKind::Video; v1.name = "V1";
    Track v2; v2.kind = TrackKind::Video; v2.name = "V2";
    Track a1; a1.kind = TrackKind::Audio; a1.name = "A1";
    Track a2; a2.kind = TrackKind::Audio; a2.name = "A2";
    p.tracks = {v1, v2, a1, a2};

    Clip c1; c1.id = "c1"; c1.track = 0;
    c1.timeline_start = 1.0; c1.duration = 4.0;
    c1.source_path = "a.mp4"; c1.name = "a.mp4";
    Clip c2; c2.id = "c2"; c2.track = 2;
    c2.timeline_start = 0.5; c2.duration = 3.0;
    c2.source_path = "b.wav"; c2.name = "b.wav";
    Clip c3; c3.id = "c3"; c3.track = 0;
    c3.timeline_start = 5.0; c3.duration = 4.0;
    c3.source_path = "a.mp4"; c3.name = "a2.mp4";
    p.clips = {c1, c2, c3};

    Marker m; m.time = 2.0; m.name = "m1";
    p.markers = {m};
    p.work_in = 1.0;
    p.work_out = 6.0;
    return p;
}

W::Event Down(float x, float y) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseDown;
    ev.position = Point{x, y};
    ev.mouse_button = W::MouseButton::Left;
    return ev;
}
W::Event Move(float x, float y) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseMove;
    ev.position = Point{x, y};
    return ev;
}
W::Event Up(float x, float y) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseUp;
    ev.position = Point{x, y};
    ev.mouse_button = W::MouseButton::Left;
    return ev;
}

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// ---- checks ---------------------------------------------------------

std::string CheckGeometry(TimelineSurface& s, const Project& p) {
    const Rect area = s.GetBounds();
    const float left = area.x + 172.0f;   // kHeaderWidth
    if (!Near(s.XAtTime(0.0), left)) return "XAtTime(0) != track left";
    const double zoom = s.PxPerSecond();
    if (!Near(s.XAtTime(1.0) - s.XAtTime(0.0), zoom)) {
        return "XAtTime slope != px_per_second";
    }
    if (!Near(s.TimeAtX(s.XAtTime(3.25)), 3.25, 1e-4)) {
        return "time<->x roundtrip broken";
    }
    s.SetScrollSeconds(2.0);
    if (!Near(s.TimeAtX(s.XAtTime(4.5)), 4.5, 1e-4)) {
        return "time<->x roundtrip broken under scroll";
    }
    s.SetScrollSeconds(0.0);

    // Track rows: first row starts under marker lane + ruler.
    const Rect r0 = s.TrackRect(0);
    const Rect r3 = s.TrackRect(3);
    if (!Near(r0.y, area.y + 18.0f + 28.0f, 0.5f)) {
        return "track 0 top != ruler bottom";
    }
    if (r3.y <= r0.y + 3.0f * 60.0f) {
        return "track stack rows not increasing";
    }
    if (s.TrackAtY(r0.y + r0.height * 0.5f) != 0) {
        return "TrackAtY(row0) != 0";
    }
    if (s.TrackAtY(r3.y + r3.height * 0.5f) != 3) {
        return "TrackAtY(row3) != 3";
    }
    if (s.TrackAtY(area.y + 2.0f) >= 0) {
        return "TrackAtY(marker lane) should be -1";
    }
    if (s.TrackAtY(area.y + area.height + 200.0f) >= 0) {
        return "TrackAtY below content should be -1";
    }

    // Clip hit-testing: body / edge-in / edge-out / empty space.
    const Rect c = s.RealClipRect(p.clips[0]);
    const auto body = s.HitClip({c.x + c.width * 0.5f,
                                 c.y + c.height * 0.5f});
    if (body.clip == nullptr || body.clip->id != "c1" ||
        body.part != ClipPart::Body) {
        return "HitClip body miss";
    }
    const auto edge_in = s.HitClip({c.x + 2.0f,
                                    c.y + c.height * 0.5f});
    if (edge_in.clip == nullptr || edge_in.part != ClipPart::EdgeIn) {
        return "HitClip edge-in miss";
    }
    const auto edge_out =
        s.HitClip({c.x + c.width - 2.0f, c.y + c.height * 0.5f});
    if (edge_out.clip == nullptr ||
        edge_out.part != ClipPart::EdgeOut) {
        return "HitClip edge-out miss";
    }
    if (s.HitClip({s.XAtTime(30.0), c.y + 4.0f}).clip != nullptr) {
        return "HitClip should miss far right of content";
    }
    return "";
}

std::string CheckGestures(TimelineSurface& s, Rec& r) {
    const Project& p = *s.GetProject();
    const Rect c1 = s.RealClipRect(p.clips[0]);
    const float cy = c1.y + c1.height * 0.5f;
    const float body_x = c1.x + c1.width * 0.5f;

    // Click-select a clip, then click empty space to clear.
    s.HandleEvent(Down(body_x, cy));
    s.HandleEvent(Up(body_x, cy));
    if (r.selects == 0 || r.last_select != "c1") {
        return "clip click did not select c1";
    }
    s.HandleEvent(Down(s.XAtTime(40.0), cy));
    s.HandleEvent(Up(s.XAtTime(40.0), cy));
    if (r.last_select != "") {
        return "empty click did not clear selection";
    }

    // Move gesture: grab c1's body, drag +40 px, release.
    // Snap OFF so the committed time tracks the pointer exactly.
    s.SetSnapEnabled(false);
    const double expected = 1.0 + 40.0 / s.PxPerSecond();
    s.HandleEvent(Down(body_x, cy));
    s.HandleEvent(Move(body_x + 40.0f, cy));
    s.HandleEvent(Up(body_x + 40.0f, cy));
    if (r.moves == 0 || r.move_id != "c1") {
        return "move gesture produced no move hook";
    }
    if (!Near(r.move_start, expected, 0.02)) {
        return "move commit start != pointer delta";
    }
    if (r.gesture_begins == 0 || r.gesture_ends == 0 ||
        r.gesture_begins != r.gesture_ends) {
        return "gesture begin/end brackets unbalanced";
    }
    const int beg_before_split = r.gesture_begins;

    // Razor split: switch tools, click mid-clip.
    s.SetTool(ToolId::Razor);
    const float cut_x = s.XAtTime(3.0);
    s.HandleEvent(Down(cut_x, cy));
    s.HandleEvent(Up(cut_x, cy));
    if (r.splits == 0 || r.split_id != "c1") {
        return "razor click produced no split hook";
    }
    if (!Near(r.split_at, 3.0, 8.0 / s.PxPerSecond() + 1e-3)) {
        return "split time not at clicked time";
    }
    s.SetTool(ToolId::Selection);

    // Snap: marker at t=2.0; the pointer grab offset is the clip's
    // center (ctx.time≈3.0, start≈1.0 -> offset 2.0), so aiming the
    // pointer at 4.05 puts the candidate start at ~2.05 — inside
    // the marker's snap window.
    s.SetSnapEnabled(true);
    const int moves_before = r.moves;
    s.HandleEvent(Down(body_x, cy));
    const float target = s.XAtTime(4.05);
    s.HandleEvent(Move(target, cy));
    s.HandleEvent(Up(target, cy));
    if (r.moves == moves_before) {
        return "snap drag produced no move";
    }
    const double snap_window = 8.0 / s.PxPerSecond();
    if (!Near(r.move_start, 2.0, snap_window + 1e-3)) {
        return "move did not snap to marker edge";
    }

    // Ruler scrub seeks.
    const int seeks_before = r.seeks;
    const float ruler_y = s.GetBounds().y + 18.0f + 14.0f + 12.0f;
    s.HandleEvent(Down(s.XAtTime(7.0), ruler_y));
    s.HandleEvent(Up(s.XAtTime(7.0), ruler_y));
    if (r.seeks == seeks_before || !Near(r.last_seek, 7.0, 0.05)) {
        return "ruler click did not seek";
    }
    (void)beg_before_split;
    return "";
}

std::string CheckChromeHooks(TimelineSurface& s, Rec& r) {
    // Track-header lock chip: rightmost button on each header's
    // bottom strip is the lock (video {eye, lock}, audio {M,S,mic,
    // lock} — slot (count-1) either way).
    const Rect header = s.HeaderRect(0);
    const float lock_x = header.x + header.width - 21.0f;
    const float chip_y = header.y + header.height - 15.0f;
    s.HandleEvent(Down(lock_x, chip_y));
    s.HandleEvent(Up(lock_x, chip_y));
    if (r.locks == 0 || !r.lock_on) {
        return "header lock chip did not fire on_track_locked";
    }
    const Rect aheader = s.HeaderRect(2);
    const float mute_x =
        aheader.x + aheader.width - 8.0f - 4.0f * 22.0f + 9.0f;
    const float achip_y = aheader.y + aheader.height - 15.0f;
    s.HandleEvent(Down(mute_x, achip_y));
    s.HandleEvent(Up(mute_x, achip_y));
    if (r.mutes == 0) {
        return "audio header M chip did not fire on_track_mute";
    }

    // Marker lane double-click adds a marker at the click time.
    const int marks_before = r.markers;
    const float lane_y = s.GetBounds().y + 9.0f;
    const float mark_x = s.XAtTime(9.0);
    s.HandleEvent(Down(mark_x, lane_y));
    s.HandleEvent(Up(mark_x, lane_y));
    s.HandleEvent(Down(mark_x, lane_y));
    s.HandleEvent(Up(mark_x, lane_y));
    if (r.markers == marks_before) {
        return "marker-lane double-click produced no marker";
    }
    if (!Near(r.last_marker, 9.0, 8.0 / s.PxPerSecond() + 1e-3)) {
        return "marker landed at wrong time";
    }

    // Work-area in-handle drag: grab at work_in's x inside the
    // ruler's top work bar, drag right, release.
    const int wa_before = r.work_areas;
    const float bar_y = s.GetBounds().y + 18.0f + 4.0f;
    s.HandleEvent(Down(s.XAtTime(1.0), bar_y));
    s.HandleEvent(Move(s.XAtTime(2.5), bar_y));
    s.HandleEvent(Up(s.XAtTime(2.5), bar_y));
    if (r.work_areas == wa_before) {
        return "work-area drag produced no set_work_area";
    }
    if (!Near(r.work_in, 2.5, 8.0 / s.PxPerSecond() + 1e-3) ||
        !Near(r.work_out, 6.0, 1e-6)) {
        return "work-area drag committed wrong range";
    }
    return "";
}

std::string CheckSnapIndicator(TimelineSurface& s) {
    // SnapCandidate is the const tool-facing seam — a resolved-
    // different result marks the guide time the Render pass draws.
    // c1's in edge sits at 1.0; the 8px window is 0.1s at zoom 80.
    const double snapped = s.SnapCandidate(1.04, 0);
    if (!Near(snapped, 1.0, 1e-9)) {
        return "candidate did not snap to the clip edge";
    }
    if (s.SnapIndicatorSeconds() != snapped) {
        return "snapped candidate did not mark the guide";
    }
    // An unsnapped call hides the guide again (drag moved off).
    if (s.SnapCandidate(3.4, 0) != 3.4 ||
        s.SnapIndicatorSeconds() != -1.0) {
        return "unsnapped candidate kept the guide marked";
    }
    // Re-mark, then the release that ends the drag clears it.
    s.SnapCandidate(1.04, 0);
    if (s.SnapIndicatorSeconds() < 0.0) {
        return "guide failed to re-mark";
    }
    s.HandleEvent(Up(s.XAtTime(4.0), s.GetBounds().y + 120.0f));
    if (s.SnapIndicatorSeconds() != -1.0) {
        return "drag release did not clear the guide";
    }
    return "";
}

std::string CheckStrip() {
    auto strip = TimelineStrip::Create();
    if (strip == nullptr || strip->Surface() == nullptr) {
        return "strip did not create a surface";
    }
    strip->SetTool(2);
    if (strip->Surface()->ActiveTool() != ToolId::RippleEdit ||
        strip->ActiveTool() != 2) {
        return "strip tool index did not reach surface";
    }
    strip->SetTool(8);
    if (strip->Surface()->ActiveTool() != ToolId::Zoom) {
        return "strip tool 8 != Zoom";
    }
    strip->SetSnapEnabled(false);
    if (strip->SnapEnabled()) return "strip snap toggle stuck";
    return "";
}

std::string CheckBridge() {
    // Bridge -> history -> document: an on_move_clip lands in the
    // model and one Undo restores it; a gesture bracket collapses a
    // trim+move burst into a single undo step (the ripple-trim
    // shape).
    Project p = MakeProject();
    TimelineHistory history(p);
    TimelineBridge bridge(p, history);
    const TimelineHooks& h = bridge.Hooks();

    if (h.on_move_clip == nullptr) return "bridge move hook unset";
    // 0.0 is free space (c3 occupies 5..9 on the same track — a
    // colliding move is correctly rejected by history).
    h.on_move_clip("c1", 0.0);
    if (!Near(FindClip(p, "c1")->timeline_start, 0.0)) {
        return "bridge move did not reach document";
    }
    if (!history.Undo()) return "move not undoable";
    if (!Near(FindClip(p, "c1")->timeline_start, 1.0)) {
        return "undo did not restore move";
    }

    // One macro per gesture: begin + trim + move + end = one undo.
    // NOTE the trim point is a SOURCE point (in/out-point space,
    // the hook contract) — c1 has in_point 0/rate 1, so out->3.0
    // lands the timeline end at 1.0 + 3.0 = 4.0.
    h.on_gesture_begin("Ripple trim");
    h.on_trim_clip("c1", TrimEdge::Out, 3.0);
    h.on_move_clip("c3", 4.0);
    h.on_gesture_end();
    const double c1_end = ClipEndSeconds(*FindClip(p, "c1"));
    const double c3_start = FindClip(p, "c3")->timeline_start;
    if (!Near(c1_end, 4.0) || !Near(c3_start, 4.0)) {
        return "macro burst edits did not land";
    }
    if (!history.Undo()) {
        return "gesture macro not undoable as one step";
    }
    if (!Near(FindClip(p, "c1")->duration, 4.0) ||
        !Near(FindClip(p, "c3")->timeline_start, 5.0)) {
        return "single undo did not revert the whole gesture";
    }

    // Ripple trim end-to-end through the bridge hook: source
    // out-point 4.0 -> 2.0 lands c1's timeline end at 3.0, freeing
    // 2 s; follower c3 slides left by the gap (5.0 -> 3.0) inside
    // the same macro.
    bridge.Hooks().on_ripple_trim("c1", TrimEdge::Out, 2.0);
    const Clip* c1 = FindClip(p, "c1");
    const Clip* c3 = FindClip(p, "c3");
    if (c1 == nullptr || c3 == nullptr) return "clips lost";
    if (!Near(ClipEndSeconds(*c1), 3.0) ||
        !Near(c3->timeline_start, 3.0)) {
        return "ripple trim did not close the freed gap";
    }
    if (!history.Undo()) return "ripple trim not undoable";
    if (!Near(FindClip(p, "c1")->duration, 4.0) ||
        !Near(FindClip(p, "c3")->timeline_start, 5.0)) {
        return "ripple-trim undo did not restore both ops";
    }

    // Delete + selection cleanup.
    bridge.SelectClip("c1");
    h.on_delete_clip("c1");
    if (FindClip(p, "c1") != nullptr) return "delete left the clip";
    if (!bridge.SelectedClipId().empty()) {
        return "delete did not clear selection";
    }
    return "";
}

}  // namespace

std::string Ui2TimelineSmoke() {
    Rec rec;
    auto surface = std::make_shared<TimelineSurface>();
    surface->SetHooks(RecordingHooks(rec));
    Project project = MakeProject();
    surface->SetProject(&project);
    surface->SetZoom(80.0);
    surface->SetTrackHeight(64.0f);
    surface->SetSnapEnabled(true);
    surface->SetPlayheadSeconds(0.0);
    surface->Layout(Rect{0.0f, 0.0f, 1400.0f, 360.0f});

    if (const std::string err = CheckGeometry(*surface, project);
        !err.empty()) {
        return "geometry: " + err;
    }
    if (const std::string err = CheckGestures(*surface, rec);
        !err.empty()) {
        return "gesture: " + err;
    }
    if (const std::string err = CheckChromeHooks(*surface, rec);
        !err.empty()) {
        return "chrome: " + err;
    }
    if (const std::string err = CheckSnapIndicator(*surface);
        !err.empty()) {
        return "snap indicator: " + err;
    }
    // Async media seam: every clip source gets probed through the
    // tri-state hooks and kPending answers return immediately.
    if (surface->PrimeMediaLookups() != 2) {
        return "media: expected 2 source paths probed";
    }
    if (rec.peaks_calls == 0 || rec.poster_calls == 0 ||
        rec.bounds_calls == 0) {
        return "media: lookup hooks were not invoked";
    }
    if (const std::string err = CheckStrip(); !err.empty()) {
        return "strip: " + err;
    }
    if (const std::string err = CheckBridge(); !err.empty()) {
        return "bridge: " + err;
    }
    return "";
}

}  // namespace XER::VideoEditor::Ui2
