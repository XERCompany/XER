// Ops suite — Phase-2 timeline ops in the corners the model/history
// suites leave open: rate-aware rolling/slide/slip deltas, asymmetric
// media handles on a rolling pair, exclusive lift boundaries,
// locked-track overwrite/insert/slide, the keyframes-vs-duration
// interaction inside SetClipRate, marker color merge, work-area
// clamping, and one undo+redo round-trip per op through
// TimelineHistory (the history suite covers each op once; this suite
// adds the snapshot-redo cases for generated-id splits and rejected
// ops pushing no undo entries).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "timeline_history.h"
#include "timeline_model.h"

namespace {
using namespace XER::VideoEditor;

// Three tracks: V1(0) video, A1(1) audio, V2(2) video — the second
// video track is where lock/other-track isolation checks live.
Project MakeProject() {
    Project p;
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.tracks.push_back(Track{TrackKind::Audio, "A1"});
    p.tracks.push_back(Track{TrackKind::Video, "V2"});
    return p;
}

Clip MakeClip(const std::string& id, int track, double start,
              double in_point, double duration,
              const std::string& source = "D:/media/a.mp4") {
    Clip c;
    c.id = id;
    c.source_path = source;
    c.track = track;
    c.timeline_start = start;
    c.in_point = in_point;
    c.duration = duration;
    return c;
}

bool NearlyEq(double a, double b) { return std::fabs(a - b) <= 1e-9; }
bool NearlyEq(float a, float b) {
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <=
           1e-6;
}

// Full-state comparator — same shape as the history suite's, extended
// with the fields it does not diff (source kind/param, audio_fx,
// marker color) so a snapshot-redo cannot silently drift on them.
bool ClipsEqual(const Clip& a, const Clip& b) {
    if (a.id != b.id || a.source_path != b.source_path ||
        a.track != b.track || a.muted != b.muted ||
        a.link_group != b.link_group || a.blend_mode != b.blend_mode ||
        a.source.kind != b.source.kind || a.source.param != b.source.param) {
        return false;
    }
    if (!NearlyEq(a.timeline_start, b.timeline_start) ||
        !NearlyEq(a.in_point, b.in_point) ||
        !NearlyEq(a.duration, b.duration) ||
        !NearlyEq(a.playback_rate, b.playback_rate) ||
        !NearlyEq(a.gain_db, b.gain_db) || !NearlyEq(a.pan, b.pan) ||
        !NearlyEq(a.fade_in, b.fade_in) ||
        !NearlyEq(a.fade_out, b.fade_out)) {
        return false;
    }
    const Transform& x = a.transform;
    const Transform& y = b.transform;
    if (!NearlyEq(x.offset_x, y.offset_x) ||
        !NearlyEq(x.offset_y, y.offset_y) ||
        !NearlyEq(x.scale, y.scale) ||
        !NearlyEq(x.rotation_deg, y.rotation_deg) ||
        !NearlyEq(x.opacity, y.opacity)) {
        return false;
    }
    if (a.masks.size() != b.masks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.masks.size(); ++i) {
        const ClipMask& ma = a.masks[i];
        const ClipMask& mb = b.masks[i];
        if (ma.shape != mb.shape || ma.inverted != mb.inverted ||
            !NearlyEq(ma.cx, mb.cx) || !NearlyEq(ma.cy, mb.cy) ||
            !NearlyEq(ma.w, mb.w) || !NearlyEq(ma.h, mb.h) ||
            !NearlyEq(ma.feather, mb.feather)) {
            return false;
        }
    }
    if (a.keyframes.size() != b.keyframes.size()) {
        return false;
    }
    for (const auto& [prop, keys] : a.keyframes) {
        const auto it = b.keyframes.find(prop);
        if (it == b.keyframes.end() ||
            it->second.size() != keys.size()) {
            return false;
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (!NearlyEq(keys[i].time, it->second[i].time) ||
                !NearlyEq(keys[i].value, it->second[i].value) ||
                keys[i].easing != it->second[i].easing) {
                return false;
            }
        }
    }
    if (a.effects.size() != b.effects.size() ||
        a.audio_fx.size() != b.audio_fx.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.effects.size(); ++i) {
        if (a.effects[i].type != b.effects[i].type ||
            a.effects[i].params.size() != b.effects[i].params.size()) {
            return false;
        }
        for (std::size_t j = 0; j < a.effects[i].params.size(); ++j) {
            if (!NearlyEq(a.effects[i].params[j],
                          b.effects[i].params[j])) {
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < a.audio_fx.size(); ++i) {
        const ClipAudioFx& fa = a.audio_fx[i];
        const ClipAudioFx& fb = b.audio_fx[i];
        if (fa.plugin_path != fb.plugin_path || fa.name != fb.name ||
            fa.enabled != fb.enabled || fa.params.size() != fb.params.size()) {
            return false;
        }
        for (const auto& [id, value] : fa.params) {
            const auto it = fb.params.find(id);
            if (it == fb.params.end() || !NearlyEq(value, it->second)) {
                return false;
            }
        }
    }
    return true;
}

bool ProjectsEqual(const Project& a, const Project& b) {
    auto fail = [](const char* what) {
        std::fprintf(stderr, "ProjectsEqual mismatch: %s\n", what);
        return false;
    };
    if (a.width != b.width || a.height != b.height ||
        a.fps_num != b.fps_num || a.fps_den != b.fps_den ||
        a.bg_color != b.bg_color) {
        return fail("project header");
    }
    if (a.tracks.size() != b.tracks.size()) {
        return fail("track count");
    }
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        const Track& x = a.tracks[i];
        const Track& y = b.tracks[i];
        if (x.kind != y.kind || x.name != y.name || x.muted != y.muted ||
            x.hidden != y.hidden || x.locked != y.locked ||
            !NearlyEq(x.gain_db, y.gain_db)) {
            return fail("track fields");
        }
    }
    if (a.clips.size() != b.clips.size()) {
        return fail("clip count");
    }
    for (std::size_t i = 0; i < a.clips.size(); ++i) {
        if (!ClipsEqual(a.clips[i], b.clips[i])) {
            std::fprintf(stderr, "ProjectsEqual mismatch: clip %zu\n", i);
            return false;
        }
    }
    if (a.transitions.size() != b.transitions.size()) {
        return fail("transition count");
    }
    for (std::size_t i = 0; i < a.transitions.size(); ++i) {
        const Transition& x = a.transitions[i];
        const Transition& y = b.transitions[i];
        if (x.from_clip != y.from_clip || x.to_clip != y.to_clip ||
            x.type != y.type || !NearlyEq(x.duration, y.duration)) {
            return fail("transition fields");
        }
    }
    if (a.markers.size() != b.markers.size()) {
        return fail("marker count");
    }
    for (std::size_t i = 0; i < a.markers.size(); ++i) {
        if (!NearlyEq(a.markers[i].time, b.markers[i].time) ||
            a.markers[i].name != b.markers[i].name ||
            a.markers[i].color != b.markers[i].color) {
            return fail("marker fields");
        }
    }
    if (!NearlyEq(a.work_in, b.work_in) ||
        !NearlyEq(a.work_out, b.work_out)) {
        return fail("work area");
    }
    return true;
}

}  // namespace

int XER_VideoEditor_RunOpsTests() {
    const MediaBounds video_bounds{100.0, true};
    const MediaBounds audio_bounds{100.0, false};
    std::string err;

    // -- RippleDeleteClip: gapped followers, surviving transitions,
    //    other tracks, locked track, audio track ----------------------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("c", 0, 10.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("d", 0, 15.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("g", 0, 40.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("au", 1, 5.0, 0.0, 20.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        AddClip(p, MakeClip("v", 2, 0.0, 0.0, 40.0), video_bounds, &err);
        XER_VE_CHECK(SetTrackLocked(p, 2, true, &err));
        XER_VE_CHECK(AddTransition(p, "c", "d",
                                   Transition::Type::Crossfade, 1.0,
                                   &err));

        XER_VE_CHECK(RippleDeleteClip(p, "b", &err));
        // Every follower on track 0 — adjacent or gapped — shifts left
        // by the deleted duration; the c->d pair moves as a unit so
        // its transition survives.
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 5.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "d")->timeline_start, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "g")->timeline_start, 35.0));
        XER_VE_CHECK(p.transitions.size() == 1);
        // Other tracks untouched (incl. the locked V2 clip).
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->timeline_start, 5.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "v")->timeline_start, 0.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->timeline_start, 0.0));

        // Deleting the head clip pulls everyone to the front; the
        // transition still holds (both endpoints shifted identically).
        XER_VE_CHECK(RippleDeleteClip(p, "a", &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 0.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "d")->timeline_start, 5.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "g")->timeline_start, 30.0));
        XER_VE_CHECK(p.transitions.size() == 1);

        // Ripple delete works on audio tracks (not video-only).
        XER_VE_CHECK(RippleDeleteClip(p, "au", &err));
        XER_VE_CHECK(FindClip(p, "au") == nullptr);
        // Locked track still refuses.
        XER_VE_CHECK(!RippleDeleteClip(p, "v", &err));
        XER_VE_CHECK(FindClip(p, "v") != nullptr);
        XER_VE_CHECK(!RippleDeleteClip(p, "zzz", &err));
    }

    // -- LiftRange: range edges are exclusive; locked tracks skipped
    //    even under close_gap ----------------------------------------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 40.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        AddClip(p, MakeClip("v", 2, 0.0, 0.0, 40.0), video_bounds,
                &err);
        XER_VE_CHECK(SetTrackLocked(p, 2, true, &err));

        // Lift [10,20): b's span == the range so it goes entirely; a
        // (end == t0) and c (start == t1) are boundary-exclusive
        // survivors — not even split.
        XER_VE_CHECK(LiftRange(p, 10.0, 20.0, false, &err));
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->timeline_start, 0.0) &&
                     NearlyEq(FindClip(p, "a")->duration, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 20.0) &&
                     NearlyEq(FindClip(p, "c")->duration, 10.0));
        // The audio clip spanning BOTH edges split twice: [0,10) keeps
        // "au", [10,20) removed, [20,40) tail under a new id.
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->duration, 10.0));
        const Clip* tail = nullptr;
        for (const auto& cl : p.clips) {
            if (cl.track == 1 && cl.id != "au") {
                tail = &cl;
            }
        }
        XER_VE_CHECK(tail != nullptr);
        XER_VE_CHECK(tail != nullptr &&
                     NearlyEq(tail->timeline_start, 20.0) &&
                     NearlyEq(tail->duration, 20.0) &&
                     NearlyEq(tail->in_point, 20.0));
        // Locked V2: no lift, no hole logic — byte-identical span.
        XER_VE_CHECK(NearlyEq(FindClip(p, "v")->timeline_start, 0.0) &&
                     NearlyEq(FindClip(p, "v")->duration, 40.0));
        XER_VE_CHECK(p.clips.size() == 5);
        // Hole [10,20) left: project still 30 s on track 0.
        XER_VE_CHECK(NearlyEq(p.duration(), 40.0));
    }
    {
        // Same layout, close_gap=true: unlocked followers >= t1 shift
        // left by (t1-t0); the locked track neither lifts nor shifts.
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 40.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        AddClip(p, MakeClip("v", 2, 0.0, 0.0, 40.0), video_bounds,
                &err);
        XER_VE_CHECK(SetTrackLocked(p, 2, true, &err));
        XER_VE_CHECK(LiftRange(p, 10.0, 20.0, true, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->duration, 10.0));
        const Clip* tail = nullptr;
        for (const auto& cl : p.clips) {
            if (cl.track == 1 && cl.id != "au") {
                tail = &cl;
            }
        }
        XER_VE_CHECK(tail != nullptr &&
                     NearlyEq(tail->timeline_start, 10.0) &&
                     NearlyEq(tail->duration, 20.0));
        // Locked track completely untouched by the extract.
        XER_VE_CHECK(NearlyEq(FindClip(p, "v")->timeline_start, 0.0) &&
                     NearlyEq(FindClip(p, "v")->duration, 40.0));
    }
    {
        // t1 beyond the project end: lifts the tail without splitting
        // at a boundary that does not exist.
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 40.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        AddClip(p, MakeClip("v", 2, 0.0, 0.0, 40.0), video_bounds,
                &err);
        XER_VE_CHECK(SetTrackLocked(p, 2, true, &err));
        XER_VE_CHECK(LiftRange(p, 25.0, 999.0, false, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->duration, 5.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->duration, 25.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "v")->duration, 40.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 10.0));
        XER_VE_CHECK(p.clips.size() == 4);
    }

    // -- RollingEdit: asymmetric media handles + rate-aware deltas ---
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 30.0, 10.0), video_bounds,
                &err);
        // `from` handle bound: +6 leaves b a 4 s head remainder, but
        // a's out would leave its 15 s media (10 + 6 > 15).
        XER_VE_CHECK(!RollingEdit(p, "a", "b", 6.0,
                                  MediaBounds{15.0, true}, video_bounds,
                                  &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->in_point, 30.0));
        // `to` handle bound: extending b's head pulls its out past
        // the 35 s media — rejected, NOTHING mutated on either side.
        XER_VE_CHECK(!RollingEdit(p, "a", "b", -8.0, video_bounds,
                                  MediaBounds{35.0, true}, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->timeline_start, 10.0) &&
                     NearlyEq(FindClip(p, "b")->in_point, 30.0));
        // Same-clip / non-adjacent pairs rejected.
        XER_VE_CHECK(!RollingEdit(p, "a", "a", 1.0, video_bounds,
                                  video_bounds, &err));
        AddClip(p, MakeClip("far", 0, 40.0, 0.0, 5.0), video_bounds,
                &err);
        XER_VE_CHECK(!RollingEdit(p, "b", "far", 1.0, video_bounds,
                                  video_bounds, &err));
        // NaN delta rejected before any mutation.
        XER_VE_CHECK(!RollingEdit(
            p, "a", "b", std::numeric_limits<double>::quiet_NaN(),
            video_bounds, video_bounds, &err));
        // Locked track rejected.
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!RollingEdit(p, "a", "b", 1.0, video_bounds,
                                  video_bounds, &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
    }
    {
        // Rate-aware pair: `to` at 2x consumes two source seconds per
        // timeline second — a 1 s roll moves b's in-point by 2.
        Project p = MakeProject();
        Clip a2 = MakeClip("a2", 0, 0.0, 0.0, 5.0);
        Clip b2 = MakeClip("b2", 0, 5.0, 40.0, 5.0);
        b2.playback_rate = 2.0;   // consumes source [40,50)
        AddClip(p, a2, video_bounds, &err);
        AddClip(p, b2, video_bounds, &err);
        XER_VE_CHECK(RollingEdit(p, "a2", "b2", 1.0, video_bounds,
                                 video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a2")->duration, 6.0));
        const Clip* b = FindClip(p, "b2");
        XER_VE_CHECK(NearlyEq(b->in_point, 42.0) &&
                     NearlyEq(b->timeline_start, 6.0) &&
                     NearlyEq(b->duration, 4.0));
        // Cut-left far enough to push b's in-point under zero: at
        // rate 2 that is only -22 timeline seconds.
        XER_VE_CHECK(!RollingEdit(p, "a2", "b2", -22.0, video_bounds,
                                  video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b2")->in_point, 42.0) &&
                     NearlyEq(FindClip(p, "a2")->duration, 6.0));
    }

    // -- SlipClip: media bound is rate-aware (rate 0.5) + lock -------
    {
        Project p = MakeProject();
        Clip s = MakeClip("s", 0, 50.0, 10.0, 8.0);
        s.playback_rate = 0.5;    // consumes 4 source seconds
        AddClip(p, s, video_bounds, &err);
        // in + duration*rate <= 100  ->  in <= 96
        XER_VE_CHECK(SlipClip(p, "s", 80.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "s")->in_point, 90.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "s")->timeline_start, 50.0) &&
                     NearlyEq(FindClip(p, "s")->duration, 8.0));
        XER_VE_CHECK(!SlipClip(p, "s", 7.0, video_bounds, &err));
        XER_VE_CHECK(!SlipClip(p, "s", -95.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "s")->in_point, 90.0));
        XER_VE_CHECK(!SlipClip(
            p, "s", std::numeric_limits<double>::quiet_NaN(),
            video_bounds, &err));
        XER_VE_CHECK(!SlipClip(p, "zzz", 1.0, video_bounds, &err));
        // Locked track: validation runs through
        // ValidateClipPlacement's lock check.
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!SlipClip(p, "s", 1.0, video_bounds, &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "s")->in_point, 90.0));
    }

    // -- SlideClip: neighbour absorption honours the neighbour's
    //    playback_rate; in_point floor and duration floor reject -----
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("m", 0, 10.0, 40.0, 5.0), video_bounds,
                &err);
        Clip n = MakeClip("n", 0, 15.0, 10.0, 10.0);
        n.playback_rate = 2.0;    // consumes source [10,30)
        AddClip(p, n, video_bounds, &err);
        // Slide right 2: a's tail absorbs the vacated head; n's head
        // trims 2 timeline seconds = 4 source seconds at rate 2.
        XER_VE_CHECK(SlideClip(p, "m", 2.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 12.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "m")->timeline_start, 12.0) &&
                     NearlyEq(FindClip(p, "m")->in_point, 40.0));
        {
            const Clip* nc = FindClip(p, "n");
            XER_VE_CHECK(NearlyEq(nc->in_point, 14.0) &&
                         NearlyEq(nc->timeline_start, 17.0) &&
                         NearlyEq(nc->duration, 8.0));
        }
        // Slide left 6: a shrinks back; n regains 6 s of head = 12
        // source seconds.
        XER_VE_CHECK(SlideClip(p, "m", -6.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 6.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "m")->timeline_start, 6.0));
        {
            const Clip* nc = FindClip(p, "n");
            XER_VE_CHECK(NearlyEq(nc->in_point, 2.0) &&
                         NearlyEq(nc->timeline_start, 11.0) &&
                         NearlyEq(nc->duration, 14.0));
        }
        // Too far left: a collapses AND n's in-point would go
        // negative — rejected, all three clips untouched.
        XER_VE_CHECK(!SlideClip(p, "m", -10.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 6.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "m")->timeline_start, 6.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "n")->in_point, 2.0) &&
                     NearlyEq(FindClip(p, "n")->duration, 14.0));
        // Too far right: n collapses — rejected.
        XER_VE_CHECK(!SlideClip(p, "m", 30.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "m")->timeline_start, 6.0));
        // Locked track rejects the whole edit.
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!SlideClip(p, "m", 1.0, video_bounds, &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(!SlideClip(
            p, "m", std::numeric_limits<double>::quiet_NaN(),
            video_bounds, &err));
        XER_VE_CHECK(!SlideClip(p, "zzz", 1.0, video_bounds, &err));
    }

    // -- SetClipRate: the resized clip is FULLY re-validated ---------
    {
        Project p = MakeProject();
        Clip a = MakeClip("a", 0, 0.0, 0.0, 10.0);
        AddClip(p, a, video_bounds, &err);
        // A keyframe at t=8 sits inside the 10 s clip...
        std::vector<Keyframe> keys = {
            {8.0, 0.5f, KeyEase::Linear}};
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Opacity, keys,
                                      &err));
        // ...but 2x shrinks the clip to 5 s — the surviving keyframe
        // range check inside ValidateClipPlacement rejects the rate
        // change, leaving rate AND duration untouched.
        XER_VE_CHECK(!SetClipRate(p, "a", 2.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->playback_rate, 1.0) &&
                     NearlyEq(FindClip(p, "a")->duration, 10.0));
        // 0.5x grows to 20 s — the key stays in range, edit applies.
        XER_VE_CHECK(SetClipRate(p, "a", 0.5, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 20.0) &&
                     NearlyEq(FindClip(p, "a")->playback_rate, 0.5));
        // Media bound re-validated against in_point + consumed span:
        // clip b at in=25 consumed 10 -> 35, so a 30 s bound rejects
        // the rate change even though consumed is invariant.
        AddClip(p, MakeClip("b", 0, 40.0, 25.0, 10.0), video_bounds,
                &err);
        XER_VE_CHECK(!SetClipRate(p, "b", 2.0, MediaBounds{30.0, true},
                                  &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->playback_rate, 1.0) &&
                     NearlyEq(FindClip(p, "b")->duration, 10.0));
        XER_VE_CHECK(SetClipRate(p, "b", 2.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->duration, 5.0) &&
                     NearlyEq(FindClip(p, "b")->playback_rate, 2.0));
        // Non-positive / non-finite rejected outright.
        XER_VE_CHECK(!SetClipRate(p, "b", 0.0, video_bounds, &err));
        XER_VE_CHECK(!SetClipRate(p, "b", -1.0, video_bounds, &err));
        XER_VE_CHECK(!SetClipRate(
            p, "b", std::numeric_limits<double>::quiet_NaN(),
            video_bounds, &err));
        XER_VE_CHECK(!SetClipRate(p, "zzz", 1.0, video_bounds, &err));
    }

    // -- InsertClipAt: only same-track clips at/after the point shift -
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 5.0), video_bounds,
                &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 30.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        // Insert inside a gap: only the >= start follower moves.
        XER_VE_CHECK(InsertClipAt(p, MakeClip("x", 0, 10.0, 0.0, 3.0),
                                  video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 23.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->timeline_start, 0.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "x")->timeline_start, 10.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->timeline_start, 0.0));
        // Insert at 0 shifts EVERY clip on the track (start >= 0).
        XER_VE_CHECK(InsertClipAt(p, MakeClip("y", 0, 0.0, 0.0, 2.0),
                                  video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->timeline_start, 2.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "x")->timeline_start, 12.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 25.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->timeline_start, 0.0));
        // Audio-track insert shifts only audio followers.
        XER_VE_CHECK(InsertClipAt(
            p, MakeClip("au2", 1, 0.0, 0.0, 5.0, "D:/m/y.mp3"),
            audio_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->timeline_start, 5.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->timeline_start, 2.0));
        // Rejected insert (empty id) commits NOTHING — the staged
        // shift is discarded.
        XER_VE_CHECK(!InsertClipAt(p, MakeClip("", 0, 50.0, 0.0, 4.0),
                                   video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "c")->timeline_start, 25.0));
        XER_VE_CHECK(p.clips.size() == 6);   // a, x, c, y + au, au2
        // Locked track rejects.
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!InsertClipAt(
            p, MakeClip("z", 0, 50.0, 0.0, 4.0), video_bounds, &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
    }

    // -- OverwriteClipAt: boundary splits, gap interiors, lock -------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 5.0), video_bounds,
                &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 30.0, "D:/m/x.mp3"),
                audio_bounds, &err);
        // Overwrite [3,12): a keeps [0,3); b's [10,12) piece is cut
        // and its [12,15) tail survives under a new id (in_point 2);
        // the gap [5,10) is simply covered.
        XER_VE_CHECK(OverwriteClipAt(
            p, MakeClip("x", 0, 3.0, 0.0, 9.0), video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 3.0));
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        const Clip* tail = nullptr;
        for (const auto& cl : p.clips) {
            if (cl.track == 0 && cl.id != "a" && cl.id != "x") {
                tail = &cl;
            }
        }
        XER_VE_CHECK(tail != nullptr &&
                     NearlyEq(tail->timeline_start, 12.0) &&
                     NearlyEq(tail->duration, 3.0) &&
                     NearlyEq(tail->in_point, 2.0));
        XER_VE_CHECK(NearlyEq(FindClip(p, "x")->timeline_start, 3.0) &&
                     NearlyEq(FindClip(p, "x")->duration, 9.0));
        // Other track untouched.
        XER_VE_CHECK(NearlyEq(FindClip(p, "au")->timeline_start, 0.0) &&
                     NearlyEq(FindClip(p, "au")->duration, 30.0));
        // Overwrite entirely inside a gap clears nothing and adds.
        XER_VE_CHECK(OverwriteClipAt(
            p, MakeClip("y", 0, 16.0, 0.0, 2.0), video_bounds, &err));
        XER_VE_CHECK(p.clips.size() == 5);
        // Invalid clip (negative duration) clears nothing: a keeps
        // its trimmed 3 s, x intact, nothing removed.
        const std::size_t n = p.clips.size();
        XER_VE_CHECK(!OverwriteClipAt(
            p, MakeClip("bad", 0, 0.0, 0.0, -1.0), video_bounds, &err));
        XER_VE_CHECK(p.clips.size() == n);
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 3.0));
        // Locked track rejects the whole overwrite.
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!OverwriteClipAt(
            p, MakeClip("z", 0, 0.0, 0.0, 1.0), video_bounds, &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(p.clips.size() == n);
    }

    // -- link group / lock-vs-transitions / work area / markers /
    //    keyframes edge cases ---------------------------------------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), video_bounds,
                &err);
        // Link group is data-only: it applies even on a LOCKED track
        // (coordinated moves are the caller's job, not a model gate).
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(SetClipLinkGroup(p, "a", "grp1"));
        XER_VE_CHECK(FindClip(p, "a")->link_group == "grp1");
        XER_VE_CHECK(SetClipLinkGroup(p, "a", ""));
        XER_VE_CHECK(!SetClipLinkGroup(p, "zzz", "g"));

        // Lock gates transitions too: add AND remove are refused on a
        // locked track, the existing transition untouched.
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 5.0), video_bounds,
                &err);
        XER_VE_CHECK(AddTransition(p, "a", "b",
                                   Transition::Type::Crossfade, 1.0,
                                   &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!AddTransition(p, "b", "c",
                                    Transition::Type::WipeLeft, 1.0,
                                    &err));
        XER_VE_CHECK(!RemoveTransition(p, "a", "b"));
        XER_VE_CHECK(p.transitions.size() == 1);
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(AddTransition(p, "b", "c",
                                   Transition::Type::WipeLeft, 1.0,
                                   &err));
        XER_VE_CHECK(RemoveTransition(p, "a", "b"));
        XER_VE_CHECK(p.transitions.size() == 1);
    }
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        // Work area clamps to [0, duration]: in > duration pins both
        // edges at the end — out <= in semantics => whole project.
        XER_VE_CHECK(SetWorkArea(p, 50.0, 60.0, &err));
        XER_VE_CHECK(NearlyEq(p.work_in, 10.0) &&
                     NearlyEq(p.work_out, 10.0));
        // A normal range round-trips and feeds snap points.
        XER_VE_CHECK(SetWorkArea(p, 3.0, 7.0, &err));
        XER_VE_CHECK(NearlyEq(p.work_in, 3.0) &&
                     NearlyEq(p.work_out, 7.0));
        const auto pts = CollectSnapPoints(p, -1, -1.0);
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 3.0) !=
                     pts.end());
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 7.0) !=
                     pts.end());
        XER_VE_CHECK(!SetWorkArea(p, -1.0, 5.0, &err));
        XER_VE_CHECK(!SetWorkArea(
            p, 0.0, std::numeric_limits<double>::quiet_NaN(), &err));

        // Marker merge: equal time replaces name AND color (no dup).
        Marker m1;
        m1.time = 5.0;
        m1.name = "first";
        m1.color = {200, 0, 0, 255};
        Marker m2;
        m2.time = 5.0;
        m2.name = "second";
        m2.color = {0, 0, 200, 128};
        XER_VE_CHECK(AddMarker(p, m1, &err));
        XER_VE_CHECK(AddMarker(p, m2, &err));
        XER_VE_CHECK(p.markers.size() == 1);
        XER_VE_CHECK(p.markers[0].name == "second");
        XER_VE_CHECK(p.markers[0].color[0] == 0 &&
                     p.markers[0].color[1] == 0 &&
                     p.markers[0].color[2] == 200 &&
                     p.markers[0].color[3] == 128);
        // Marker at t=0 is legal; removal tolerates epsilon drift.
        Marker m0;
        m0.time = 0.0;
        m0.name = "zero";
        XER_VE_CHECK(AddMarker(p, m0, &err));
        XER_VE_CHECK(p.markers.size() == 2);
        XER_VE_CHECK(p.markers[0].time == 0.0);
        XER_VE_CHECK(RemoveMarker(p, 5.0 + 5e-7));   // within 1e-6
        XER_VE_CHECK(p.markers.size() == 1);
        XER_VE_CHECK(!RemoveMarker(p, 99.0));
        Marker bad;
        bad.time = -1.0;
        XER_VE_CHECK(!AddMarker(p, bad, &err));
        bad.time = std::numeric_limits<double>::quiet_NaN();
        XER_VE_CHECK(!AddMarker(p, bad, &err));
    }
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        // Boundary key at exactly `duration` is legal; epsilon past
        // it, negative, and NaN times are rejected.
        std::vector<Keyframe> edge = {{10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Scale, edge,
                                      &err));
        std::vector<Keyframe> over = {
            {10.001, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(!SetClipKeyframes(p, "a", KeyProp::Scale, over,
                                       &err));
        std::vector<Keyframe> neg = {
            {-0.5, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(!SetClipKeyframes(p, "a", KeyProp::Scale, neg,
                                       &err));
        std::vector<Keyframe> nan_key = {
            {std::numeric_limits<double>::quiet_NaN(), 1.0f,
             KeyEase::Linear}};
        XER_VE_CHECK(!SetClipKeyframes(p, "a", KeyProp::Scale, nan_key,
                                       &err));
        // The surviving list is exactly the boundary key.
        const auto& stored =
            FindClip(p, "a")->keyframes.at(KeyProp::Scale);
        XER_VE_CHECK(stored.size() == 1 &&
                     NearlyEq(stored[0].time, 10.0));
        // Setting one prop leaves other props' lists alone; clearing
        // one prop keeps the rest.
        std::vector<Keyframe> op = {{0.0, 0.2f, KeyEase::Hold},
                                    {10.0, 1.0f, KeyEase::EaseInOut}};
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Opacity, op,
                                      &err));
        XER_VE_CHECK(FindClip(p, "a")->keyframes.size() == 2);
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Scale, {},
                                      &err));
        XER_VE_CHECK(FindClip(p, "a")->keyframes.size() == 1);
        XER_VE_CHECK(FindClip(p, "a")->keyframes.count(
                         KeyProp::Opacity) == 1);
        // Unknown clip fails; locked track fails via validation.
        XER_VE_CHECK(!SetClipKeyframes(p, "zzz", KeyProp::Opacity, op,
                                       &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!SetClipKeyframes(p, "a", KeyProp::Rotation, op,
                                       &err));
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(FindClip(p, "a")->keyframes.size() == 1);
    }

    // ================================================================
    // History round-trips: one undo+redo per op. The LiftRange and
    // OverwriteClipAt cases also pin deterministic redo of generated
    // split-piece ids (snapshot commands replay them verbatim).
    // ================================================================

    // -- history: RippleDeleteClip restores clip + dead transition ---
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), video_bounds,
                &err);
        AddClip(p, MakeClip("c", 0, 10.0, 0.0, 5.0), video_bounds,
                &err);
        AddTransition(p, "b", "c", Transition::Type::Crossfade, 1.0,
                      &err);
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.RippleDeleteClip("b", &err));
        XER_VE_CHECK(p.transitions.empty());
        const Project rippled = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, rippled));
    }

    // -- history: LiftRange hole (close_gap=false) --------------------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), video_bounds,
                &err);
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.LiftRange(5.0, 15.0, false, &err));
        // a keeps [0,5); b's [15,20) tail survives under a NEW id
        // (the removed inside piece kept "b") — the hole [5,15) stays
        // open.
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 5.0));
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        XER_VE_CHECK(p.clips.size() == 2);
        const Project lifted = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, lifted));   // same generated ids
    }

    // -- history: RollingEdit / SlipClip / SlideClip / SetClipRate ----
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 30.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("n", 0, 20.0, 5.0, 10.0), video_bounds,
                &err);
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.RollingEdit("a", "b", 1.0, video_bounds,
                                         video_bounds, &err));
        const Project rolled = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, rolled));

        const Project pre_slip = p;
        XER_VE_CHECK(history.SlipClip("a", 3.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->in_point, 3.0));
        const Project slipped = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, pre_slip));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, slipped));

        // a ends at 11, b spans [11,20), n starts at 20 — a|b|n are
        // adjacent, so sliding b lets both neighbours absorb.
        const Project pre_slide = p;
        XER_VE_CHECK(history.SlideClip("b", 2.0, video_bounds, &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "a")->duration, 13.0) &&
                     NearlyEq(FindClip(p, "n")->timeline_start, 22.0));
        const Project slid = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, pre_slide));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, slid));

        // SetClipRate on b (now [13,22), dur 9): rate 2 halves its
        // 9 s span to 4.5.
        const Project pre_rate = p;
        XER_VE_CHECK(history.SetClipRate("b", 2.0, video_bounds,
                                         &err));
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->duration, 4.5));
        const Project rated = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, pre_rate));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, rated));
    }

    // -- history: InsertClipAt / OverwriteClipAt generated-id redo ----
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), video_bounds,
                &err);
        TimelineHistory history(p);
        const Project before = p;
        std::string id;
        XER_VE_CHECK(history.InsertClipAt(
            MakeClip("", 0, 5.0, 0.0, 3.0), video_bounds, &id, &err));
        XER_VE_CHECK(!id.empty());
        XER_VE_CHECK(NearlyEq(FindClip(p, "b")->timeline_start, 8.0));
        const Project inserted = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, inserted));
        // Overwrite [2,6): splits a at 2 and the inserted clip at 6 —
        // the surviving [6,8) tail mints a fresh id that redo must
        // reproduce verbatim.
        const Project pre_over = p;
        XER_VE_CHECK(history.OverwriteClipAt(
            MakeClip("", 0, 2.0, 0.0, 4.0), video_bounds, &id, &err));
        XER_VE_CHECK(!id.empty());
        const Project overwritten = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, pre_over));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, overwritten));
    }

    // -- history: markers / work area / lock / link / keyframes -------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        // Pre-seed non-default state so undo restores something
        // meaningful (the history suite only covered empty -> set).
        std::vector<Keyframe> first = {
            {0.0, 0.1f, KeyEase::Linear}};
        SetClipKeyframes(p, "a", KeyProp::Opacity, first, &err);
        SetClipLinkGroup(p, "a", "g0");
        TimelineHistory history(p);

        const Project seeded = p;
        std::vector<Keyframe> second = {
            {0.0, 0.5f, KeyEase::Hold},
            {10.0, 1.0f, KeyEase::EaseOut}};
        XER_VE_CHECK(history.SetClipKeyframes("a", KeyProp::Opacity,
                                              second, &err));
        XER_VE_CHECK(FindClip(p, "a")
                         ->keyframes.at(KeyProp::Opacity)
                         .size() == 2);
        XER_VE_CHECK(history.Undo());           // back to the 1-key list
        XER_VE_CHECK(FindClip(p, "a")
                         ->keyframes.at(KeyProp::Opacity)
                         .size() == 1);
        XER_VE_CHECK(ProjectsEqual(p, seeded));
        XER_VE_CHECK(history.Redo());

        XER_VE_CHECK(history.SetClipLinkGroup("a", "g1"));
        XER_VE_CHECK(FindClip(p, "a")->link_group == "g1");
        XER_VE_CHECK(history.Undo());           // restores "g0", not ""
        XER_VE_CHECK(FindClip(p, "a")->link_group == "g0");
        XER_VE_CHECK(history.Redo());

        Marker m;
        m.time = 4.0;
        m.name = "mid";
        m.color = {10, 20, 30, 40};
        XER_VE_CHECK(history.AddMarker(m, &err));
        const Project with_marker = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(p.markers.empty());
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, with_marker));
        XER_VE_CHECK(p.markers[0].color[2] == 30);
        XER_VE_CHECK(history.RemoveMarker(4.0));
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(p.markers.size() == 1);

        XER_VE_CHECK(history.SetWorkArea(2.0, 8.0, &err));
        XER_VE_CHECK(NearlyEq(p.work_in, 2.0) &&
                     NearlyEq(p.work_out, 8.0));
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(p.work_out <= p.work_in);
        XER_VE_CHECK(history.Redo());

        XER_VE_CHECK(history.SetTrackLocked(0, true, &err));
        XER_VE_CHECK(p.tracks[0].locked);
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(!p.tracks[0].locked);
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(p.tracks[0].locked);
    }

    // -- history: rejected ops push NO undo entries --------------------
    {
        Project p = MakeProject();
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), video_bounds,
                &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 5.0), video_bounds,
                &err);
        TimelineHistory history(p);
        const Project settled = p;
        // Every phase-2 op's rejection path must leave the stack AND
        // the project untouched.
        XER_VE_CHECK(!history.RippleDeleteClip("zzz", &err));
        XER_VE_CHECK(!history.LiftRange(9.0, 4.0, true, &err));
        XER_VE_CHECK(!history.RollingEdit("a", "b", 50.0, video_bounds,
                                          video_bounds, &err));
        XER_VE_CHECK(!history.SlipClip("a", 500.0, video_bounds,
                                       &err));
        XER_VE_CHECK(!history.SlideClip("a", -50.0, video_bounds,
                                        &err));
        XER_VE_CHECK(!history.SetClipRate("a", 0.0, video_bounds,
                                          &err));
        XER_VE_CHECK(!history.InsertClipAt(
            MakeClip("", 0, 3.0, 0.0, 4.0), video_bounds, nullptr,
            &err));   // lands inside a
        XER_VE_CHECK(!history.OverwriteClipAt(
            MakeClip("", 0, 0.0, 0.0, -1.0), video_bounds, nullptr,
            &err));   // invalid duration
        std::vector<Keyframe> out_of_range = {
            {50.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(!history.SetClipKeyframes(
            "a", KeyProp::Opacity, out_of_range, &err));
        Marker bad;
        bad.time = -2.0;
        XER_VE_CHECK(!history.AddMarker(bad, &err));
        XER_VE_CHECK(!history.RemoveMarker(99.0));
        XER_VE_CHECK(!history.SetWorkArea(-1.0, 5.0, &err));
        XER_VE_CHECK(!history.SetTrackLocked(9, true, &err));
        XER_VE_CHECK(!history.SetClipLinkGroup("zzz", "g"));
        XER_VE_CHECK(!history.CanUndo());
        XER_VE_CHECK(!history.CanRedo());
        XER_VE_CHECK(ProjectsEqual(p, settled));
    }

    return XER::VideoEditor::Test::Failures();
}
