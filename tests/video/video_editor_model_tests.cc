#include <algorithm>
#include <cmath>
#include <limits>

#include "video_editor_test_util.h"
#include "timeline_model.h"

namespace {
using namespace XER::VideoEditor;

Project MakeProject() {
    Project p;
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.tracks.push_back(Track{TrackKind::Audio, "A1"});
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

}  // namespace

int XER_VideoEditor_RunModelTests() {
    // -- defaults / queries -------------------------------------------
    {
        Project p = MakeProject();
        XER_VE_CHECK(p.duration() == 0.0);
        XER_VE_CHECK(FindClip(p, "nope") == nullptr);
        XER_VE_CHECK(FindTrack(p, 1) != nullptr &&
                     FindTrack(p, 1)->kind == TrackKind::Audio);
        XER_VE_CHECK(FindTrack(p, 7) == nullptr);
        XER_VE_CHECK(!NewClipId().empty());
        // ids are unique across calls
        XER_VE_CHECK(NewClipId() != NewClipId());
    }

    // -- AddClip + overlap rejection ----------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        XER_VE_CHECK(AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds,
                             &err));
        // overlapping on same track rejected
        XER_VE_CHECK(!AddClip(p, MakeClip("b", 0, 4.0, 0.0, 5.0), bounds,
                              &err));
        // adjacent (end == start) accepted
        XER_VE_CHECK(AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds,
                             &err));
        // same span on the OTHER track is fine
        XER_VE_CHECK(AddClip(p, MakeClip("c", 1, 0.0, 0.0, 5.0,
                                       "D:/media/a.mp3"),
                             MediaBounds{100.0, false}, &err));
        // duplicate id rejected
        XER_VE_CHECK(!AddClip(p, MakeClip("a", 0, 20.0, 0.0, 1.0), bounds,
                              &err));
        // trim bounds: in+duration > media duration rejected
        XER_VE_CHECK(!AddClip(p, MakeClip("d", 0, 30.0, 90.0, 20.0),
                              bounds, &err));
        // audio-only file on a VIDEO track rejected, on AUDIO ok
        XER_VE_CHECK(!AddClip(p, MakeClip("e", 0, 40.0, 0.0, 5.0,
                                        "D:/media/a.mp3"),
                              MediaBounds{100.0, false}, &err));
        XER_VE_CHECK(AddClip(p, MakeClip("e", 1, 40.0, 0.0, 5.0,
                                       "D:/media/a.mp3"),
                             MediaBounds{100.0, false}, &err));
        // bad track index / negative start / zero duration rejected
        XER_VE_CHECK(!AddClip(p, MakeClip("f", 9, 0.0, 0.0, 1.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("f", 0, -1.0, 0.0, 1.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("f", 0, 0.0, 0.0, 0.0), bounds,
                              &err));
        XER_VE_CHECK(p.duration() == 45.0);   // clip "e" ends at 45
    }

    // -- MoveClip ------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 5.0), bounds, &err);
        XER_VE_CHECK(MoveClip(p, "a", 5.0, bounds, &err));   // flush to b
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 5.0) < 1e-9);
        XER_VE_CHECK(!MoveClip(p, "a", 12.0, bounds, &err)); // overlaps b
        XER_VE_CHECK(!MoveClip(p, "a", -1.0, bounds, &err));
    }

    // -- TrimClip --------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};   // 100 s source
        AddClip(p, MakeClip("a", 0, 10.0, 20.0, 8.0), bounds, &err);
        // in-trim: new in_point 22 → start 12, duration 6 (out stays 28)
        XER_VE_CHECK(TrimClip(p, "a", TrimEdge::In, 22.0, bounds, &err));
        const Clip* c = FindClip(p, "a");
        XER_VE_CHECK(std::abs(c->in_point - 22.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->timeline_start - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->duration - 6.0) < 1e-9);
        // out-trim: new out_point 26 → duration 4 (in stays 22)
        XER_VE_CHECK(TrimClip(p, "a", TrimEdge::Out, 26.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 4.0) < 1e-9);
        // clamps: out beyond media end rejected
        XER_VE_CHECK(!TrimClip(p, "a", TrimEdge::Out, 200.0, bounds, &err));
        // in-trim below 0 rejected
        XER_VE_CHECK(!TrimClip(p, "a", TrimEdge::In, -1.0, bounds, &err));
        // trim that would collapse duration rejected
        XER_VE_CHECK(!TrimClip(p, "a", TrimEdge::Out, 22.0, bounds, &err));
    }

    // -- SplitClip -------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        Clip c = MakeClip("a", 0, 10.0, 20.0, 10.0);
        c.fade_in = 1.0; c.fade_out = 2.0;
        AddClip(p, c, bounds, &err);
        std::string id2;
        XER_VE_CHECK(SplitClip(p, "a", 14.0, "b", &err));
        XER_VE_CHECK(id2.empty());  // id supplied by caller
        const Clip* a = FindClip(p, "a");
        const Clip* b = FindClip(p, "b");
        XER_VE_CHECK(a != nullptr && b != nullptr);
        XER_VE_CHECK(std::abs(a->duration - 4.0) < 1e-9);   // 10→14
        XER_VE_CHECK(std::abs(b->timeline_start - 14.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->in_point - 24.0) < 1e-9);  // 20+4
        XER_VE_CHECK(std::abs(b->duration - 6.0) < 1e-9);
        XER_VE_CHECK(a->fade_in == 1.0 && a->fade_out == 0.0);
        XER_VE_CHECK(b->fade_in == 0.0 && b->fade_out == 2.0);
        // split outside the clip / at edges rejected
        XER_VE_CHECK(!SplitClip(p, "a", 5.0, "x", &err));
        XER_VE_CHECK(!SplitClip(p, "a", 10.0, "x", &err));   // == start
        XER_VE_CHECK(!SplitClip(p, "zzz", 12.0, "x", &err));
        // duplicate new id rejected
        XER_VE_CHECK(!SplitClip(p, "a", 12.0, "a", &err));
    }

    // -- Transitions -----------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("c", 1, 0.0, 0.0, 5.0, "D:/m/x.mp3"),
                MediaBounds{100.0, false}, &err);
        // adjacent same-track video clips: ok
        XER_VE_CHECK(AddTransition(p, "a", "b", Transition::Type::Crossfade,
                                   1.0, &err));
        // non-adjacent (gap) rejected — move "b" away first
        MoveClip(p, "b", 6.0, bounds, &err);
        XER_VE_CHECK(p.transitions.empty());   // move dropped it!
        XER_VE_CHECK(!AddTransition(p, "a", "b", Transition::Type::Crossfade,
                                    1.0, &err));
        MoveClip(p, "b", 5.0, bounds, &err);
        XER_VE_CHECK(p.transitions.empty());   // dropped stays dropped
        XER_VE_CHECK(AddTransition(p, "a", "b", Transition::Type::Crossfade,
                                   1.0, &err));
        // same cut duplicate rejected (pair-level, any type)
        XER_VE_CHECK(!AddTransition(p, "a", "b", Transition::Type::WipeLeft,
                                    1.0, &err));
        // ClipTransitionHandles: the a->b cut at t=5 with D=1.0 gives
        // each endpoint duration/2 = 0.5 s of handle on the cut side;
        // clip "c" sits on the audio track (no transitions) -> {0,0}.
        {
            const auto ha = ClipTransitionHandles(p, "a");
            const auto hb = ClipTransitionHandles(p, "b");
            const auto hc = ClipTransitionHandles(p, "c");
            const auto h_missing = ClipTransitionHandles(p, "nope");
            XER_VE_CHECK(ha.first == 0.0 && ha.second == 0.5);
            XER_VE_CHECK(hb.first == 0.5 && hb.second == 0.0);
            XER_VE_CHECK(hc.first == 0.0 && hc.second == 0.0);
            XER_VE_CHECK(h_missing.first == 0.0 &&
                         h_missing.second == 0.0);
        }
        // A clip between TWO transitions gets both handles: "v" sits
        // adjacent after "b" on the video track; b->v D=2.0 adds a
        // 1.0 s out-handle to "b" and a 1.0 s in-handle to "v".
        AddClip(p, MakeClip("v", 0, 10.0, 0.0, 5.0), bounds, &err);
        XER_VE_CHECK(AddTransition(p, "b", "v",
                                   Transition::Type::SlideLeft, 2.0,
                                   &err));
        {
            const auto hb = ClipTransitionHandles(p, "b");
            const auto hv = ClipTransitionHandles(p, "v");
            XER_VE_CHECK(hb.first == 0.5 && hb.second == 1.0);
            XER_VE_CHECK(hv.first == 1.0 && hv.second == 0.0);
        }
        XER_VE_CHECK(RemoveTransition(p, "b", "v"));
        // audio track clips can't take transitions (kind check): use two
        // adjacent audio clips
        AddClip(p, MakeClip("d", 1, 5.0, 0.0, 5.0, "D:/m/y.mp3"),
                MediaBounds{100.0, false}, &err);
        XER_VE_CHECK(!AddTransition(p, "c", "d",
                                    Transition::Type::Crossfade, 1.0, &err));
        // reversed order rejected (from must precede to)
        XER_VE_CHECK(!AddTransition(p, "b", "a",
                                    Transition::Type::Crossfade, 1.0, &err));
        // RemoveTransition
        XER_VE_CHECK(RemoveTransition(p, "a", "b"));
        XER_VE_CHECK(p.transitions.empty());
    }

    // -- SplitClip transition remap --------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("x", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("a", 0, 5.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("y", 0, 15.0, 0.0, 5.0), bounds, &err);
        AddTransition(p, "x", "a", Transition::Type::Crossfade, 1.0, &err);
        AddTransition(p, "a", "y", Transition::Type::WipeLeft, 1.0, &err);
        XER_VE_CHECK(SplitClip(p, "a", 10.0, "a2", &err));
        // x->a(now first half) kept; a->y re-pointed to a2
        bool saw_xa = false, saw_a2y = false, saw_ay = false;
        for (const auto& t : p.transitions) {
            if (t.from_clip == "x" && t.to_clip == "a") saw_xa = true;
            if (t.from_clip == "a2" && t.to_clip == "y") saw_a2y = true;
            if (t.from_clip == "a" && t.to_clip == "y") saw_ay = true;
        }
        XER_VE_CHECK(saw_xa && saw_a2y && !saw_ay);
    }

    // -- RemoveClip drops transitions ------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds, &err);
        AddTransition(p, "a", "b", Transition::Type::Crossfade, 1.0, &err);
        XER_VE_CHECK(RemoveClip(p, "a"));
        XER_VE_CHECK(p.transitions.empty());   // referenced "a" → dropped
        XER_VE_CHECK(FindClip(p, "b") != nullptr);
        // NOTE: AddClip can never break a VALID transition — a valid
        // transition requires exact adjacency (no gap to insert into),
        // and overlap validation forbids landing inside either clip.
        // DropInvalidTransitions after AddClip is defensive only.
    }

    // -- ReplaceClip / RemoveClip / tracks / snap -------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        Clip repl = *FindClip(p, "a");
        repl.gain_db = -6.0f;
        repl.track = 1;   // move to audio track — video file allowed
        repl.in_point = 1.0;
        XER_VE_CHECK(ReplaceClip(p, "a", repl, bounds, &err));
        const Clip* c = FindClip(p, "a");
        XER_VE_CHECK(c->track == 1 && std::abs(c->gain_db - -6.0f) < 1e-6);
        XER_VE_CHECK(std::abs(c->in_point - 1.0) < 1e-9);
        // ReplaceClip onto an occupied spot rejected
        AddClip(p, MakeClip("b", 0, 0.0, 0.0, 5.0), bounds, &err);
        Clip bad = *FindClip(p, "a");
        bad.track = 0; bad.timeline_start = 1.0;  // overlaps "b"
        XER_VE_CHECK(!ReplaceClip(p, "a", bad, bounds, &err));
        // track0 now: "b" 0-5; add "d" 5-10 (adjacent, no overlap)
        AddClip(p, MakeClip("d", 0, 5.0, 0.0, 5.0), bounds, &err);
        // RemoveTrack: non-empty rejected
        XER_VE_CHECK(!RemoveTrack(p, 0, &err));   // "b","d" live here
        int t2 = AddTrack(p, TrackKind::Audio, "A2");
        XER_VE_CHECK(t2 == 2);
        XER_VE_CHECK(!RemoveTrack(p, 1, &err));   // "a" lives on track 1
        // move "a" (video file) to track 0 at a free slot 10-15
        Clip ra = *FindClip(p, "a");
        ra.track = 0; ra.timeline_start = 10.0;
        XER_VE_CHECK(ReplaceClip(p, "a", ra, bounds, &err));
        XER_VE_CHECK(RemoveTrack(p, 1, &err));    // A1 now empty
        XER_VE_CHECK(p.tracks.size() == 2);       // [V1, A2]
        XER_VE_CHECK(FindClip(p, "a")->track == 0);
        // snap helpers — track0 clips: b 0-5, d 5-10, a 10-15
        const auto pts = CollectSnapPoints(p, 0, 3.3);
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 3.3) != pts.end());
        XER_VE_CHECK(std::abs(SnapTime(p, 0, 4.9, 0.5, -1.0) - 5.0) < 1e-9);
        XER_VE_CHECK(SnapTime(p, 0, 4.9, 0.05, -1.0) == 4.9);  // too far
        XER_VE_CHECK(std::abs(SnapTime(p, 0, 3.4, 0.5, 3.3) - 3.3) < 1e-9);
    }

    // -- non-finite input rejection (NaN / ±Inf) ----------------------
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        const float fnan = std::numeric_limits<float>::quiet_NaN();
        const float finf = std::numeric_limits<float>::infinity();
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};

        // AddClip: ValidateClipPlacement guards every double field —
        // NaN defeats all relational checks, so it must be rejected
        // explicitly or it would also evade overlap detection forever.
        XER_VE_CHECK(!AddClip(p, MakeClip("n1", 0, nan, 0.0, 5.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("n2", 0, 0.0, nan, 5.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("n3", 0, 0.0, 0.0, nan), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("i1", 0, inf, 0.0, 5.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("i2", 0, 0.0, inf, 5.0), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("i3", 0, 0.0, 0.0, inf), bounds,
                              &err));
        XER_VE_CHECK(!AddClip(p, MakeClip("i4", 0, -inf, 0.0, 5.0),
                              bounds, &err));
        XER_VE_CHECK(p.clips.empty());   // nothing committed

        // Finite fields whose sum overflows to inf are rejected too —
        // with unknown media bounds there is no upper clamp otherwise.
        XER_VE_CHECK(!AddClip(p, MakeClip("big", 0, 0.0, 1e308, 1e308),
                              MediaBounds{0.0, true}, &err));
        XER_VE_CHECK(p.clips.empty());

        // Presentation fields persist through Save — same contract.
        Clip fx = MakeClip("fx", 0, 0.0, 0.0, 5.0);
        fx.gain_db = fnan;
        XER_VE_CHECK(!AddClip(p, fx, bounds, &err));
        fx.gain_db = 0.0f;
        fx.fade_in = inf;
        XER_VE_CHECK(!AddClip(p, fx, bounds, &err));
        fx.fade_in = 0.0;
        fx.transform.opacity = fnan;
        XER_VE_CHECK(!AddClip(p, fx, bounds, &err));
        fx.transform = Transform{};
        fx.effects.push_back(ClipEffect{"blur", {finf}});
        XER_VE_CHECK(!AddClip(p, fx, bounds, &err));
        XER_VE_CHECK(p.clips.empty());

        // The remaining mutators inherit the guard (directly or via
        // ValidateClipPlacement).
        XER_VE_CHECK(AddClip(p, MakeClip("m", 0, 0.0, 0.0, 5.0), bounds,
                             &err));
        XER_VE_CHECK(AddClip(p, MakeClip("m2", 0, 5.0, 0.0, 5.0), bounds,
                             &err));
        XER_VE_CHECK(!MoveClip(p, "m", nan, bounds, &err));
        XER_VE_CHECK(!MoveClip(p, "m", inf, bounds, &err));
        XER_VE_CHECK(!TrimClip(p, "m", TrimEdge::In, nan, bounds, &err));
        XER_VE_CHECK(!TrimClip(p, "m", TrimEdge::Out, nan, bounds, &err));
        XER_VE_CHECK(!TrimClip(p, "m", TrimEdge::In, inf, bounds, &err));
        XER_VE_CHECK(!TrimClip(p, "m", TrimEdge::Out, -inf, bounds, &err));
        XER_VE_CHECK(!SplitClip(p, "m", nan, "s1", &err));
        XER_VE_CHECK(!SplitClip(p, "m", inf, "s1", &err));
        XER_VE_CHECK(FindClip(p, "s1") == nullptr);
        XER_VE_CHECK(p.clips.size() == 2);   // unsplit, unmoved
        XER_VE_CHECK(!AddTransition(p, "m", "m2",
                                    Transition::Type::Crossfade, nan,
                                    &err));
        XER_VE_CHECK(!AddTransition(p, "m", "m2",
                                    Transition::Type::Crossfade, inf,
                                    &err));
        XER_VE_CHECK(!AddTransition(p, "m", "m2",
                                    Transition::Type::Crossfade, -inf,
                                    &err));
        XER_VE_CHECK(p.transitions.empty());

        // ReplaceClip re-validates the whole replacement clip — a
        // non-finite field is rejected and the stored clip is untouched.
        Clip repl = *FindClip(p, "m");
        repl.transform.rotation_deg = fnan;
        XER_VE_CHECK(!ReplaceClip(p, "m", repl, bounds, &err));
        repl = *FindClip(p, "m");
        repl.gain_db = finf;
        XER_VE_CHECK(!ReplaceClip(p, "m", repl, bounds, &err));
        const Clip* m = FindClip(p, "m");
        XER_VE_CHECK(m->transform.rotation_deg == 0.0f &&
                     m->gain_db == 0.0f);
        XER_VE_CHECK(std::isfinite(m->timeline_start) &&
                     std::isfinite(m->duration));
    }

    // -- RippleDeleteClip ------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("c", 0, 10.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 5.0, "D:/m/x.mp3"),
                MediaBounds{100.0, false}, &err);
        XER_VE_CHECK(RippleDeleteClip(p, "b", &err));
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        // same-track followers close the gap; other tracks untouched
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 5.0) <
                     1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "au")->timeline_start) < 1e-9);
        // unknown id / locked track fail without mutating
        XER_VE_CHECK(!RippleDeleteClip(p, "zzz", &err));
        p.tracks[0].locked = true;
        XER_VE_CHECK(!RippleDeleteClip(p, "c", &err));
        XER_VE_CHECK(FindClip(p, "c") != nullptr &&
                     std::abs(FindClip(p, "c")->timeline_start - 5.0) <
                         1e-9);
    }

    // -- LiftRange (lift — hole left) -----------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), bounds, &err);
        // lift [5,15): a keeps [0,5); b's inside piece [10,15) is
        // removed and its [15,20) tail survives under a NEW id (the id
        // stays on the first split piece, which is the removed one);
        // c untouched.
        XER_VE_CHECK(LiftRange(p, 5.0, 15.0, false, &err));
        XER_VE_CHECK(p.clips.size() == 3);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 5.0) < 1e-9);
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 20.0) <
                     1e-9);
        const Clip* tail = nullptr;
        for (const auto& cl : p.clips) {
            if (cl.id != "a" && cl.id != "c") {
                tail = &cl;
            }
        }
        XER_VE_CHECK(tail != nullptr);
        XER_VE_CHECK(std::abs(tail->timeline_start - 15.0) < 1e-9);
        XER_VE_CHECK(std::abs(tail->duration - 5.0) < 1e-9);
        XER_VE_CHECK(std::abs(tail->in_point - 5.0) < 1e-9);
        XER_VE_CHECK(std::abs(p.duration() - 30.0) < 1e-9);   // hole left
    }

    // -- LiftRange (extract — close_gap) --------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 40.0, "D:/m/x.mp3"),
                MediaBounds{100.0, false}, &err);
        XER_VE_CHECK(LiftRange(p, 5.0, 15.0, true, &err));
        // video: a[0,5], b-tail[5,10], c[10,20] — gap closed
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 10.0) <
                     1e-9);
        // audio [0,40) spans the range: [0,5) keeps the id, the
        // [15,40) tail shifts to [5,30)
        XER_VE_CHECK(std::abs(FindClip(p, "au")->duration - 5.0) < 1e-9);
        XER_VE_CHECK(std::abs(p.duration() - 30.0) < 1e-9);
        // invalid ranges are rejected and leave the project untouched
        const std::size_t n = p.clips.size();
        XER_VE_CHECK(!LiftRange(p, 10.0, 10.0, true, &err));
        XER_VE_CHECK(!LiftRange(p, 12.0, 8.0, true, &err));
        XER_VE_CHECK(!LiftRange(p, -2.0, 8.0, true, &err));
        XER_VE_CHECK(!LiftRange(
            p, std::numeric_limits<double>::quiet_NaN(), 8.0, true,
            &err));
        XER_VE_CHECK(p.clips.size() == n);
    }

    // -- LiftRange skips locked tracks silently --------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("au", 1, 0.0, 0.0, 10.0, "D:/m/x.mp3"),
                MediaBounds{100.0, false}, &err);
        p.tracks[0].locked = true;
        XER_VE_CHECK(LiftRange(p, 2.0, 8.0, false, &err));
        // locked video track untouched; audio track still lifted
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 10.0) < 1e-9);
        XER_VE_CHECK(p.clips.size() == 3);   // au: [0,2) + [8,10)
        XER_VE_CHECK(std::abs(FindClip(p, "au")->duration - 2.0) < 1e-9);
    }

    // -- RollingEdit ------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 30.0, 10.0), bounds, &err);
        // delta +2: cut right — a grows to 12, b's head gives up 2 s
        XER_VE_CHECK(RollingEdit(p, "a", "b", 2.0, bounds, bounds, &err));
        const Clip* a = FindClip(p, "a");
        const Clip* b = FindClip(p, "b");
        XER_VE_CHECK(std::abs(a->duration - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->in_point - 32.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->timeline_start - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->duration - 8.0) < 1e-9);
        // delta -5: cut left — a shrinks to 7, b extends into [7,20)
        XER_VE_CHECK(RollingEdit(p, "a", "b", -5.0, bounds, bounds,
                                 &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 7.0) < 1e-9);
        b = FindClip(p, "b");
        XER_VE_CHECK(std::abs(b->in_point - 27.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->timeline_start - 7.0) < 1e-9);
        XER_VE_CHECK(std::abs(b->duration - 13.0) < 1e-9);
        // past either handle: +20 collapses b, -40 pushes b's in-point
        // below zero — both rejected with NOTHING mutated
        XER_VE_CHECK(!RollingEdit(p, "a", "b", 20.0, bounds, bounds,
                                  &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 7.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "b")->duration - 13.0) < 1e-9);
        XER_VE_CHECK(!RollingEdit(p, "a", "b", -40.0, bounds, bounds,
                                  &err));
        XER_VE_CHECK(std::abs(FindClip(p, "b")->in_point - 27.0) < 1e-9);
        // non-adjacent pair / locked track rejected
        AddClip(p, MakeClip("c", 0, 30.0, 0.0, 5.0), bounds, &err);
        XER_VE_CHECK(!RollingEdit(p, "b", "c", 1.0, bounds, bounds,
                                  &err));
        p.tracks[0].locked = true;
        XER_VE_CHECK(!RollingEdit(p, "a", "b", 1.0, bounds, bounds,
                                  &err));
        p.tracks[0].locked = false;
    }

    // -- SlipClip ---------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 10.0, 20.0, 8.0), bounds, &err);
        // slip +5: the source window slides, the timeline is fixed
        XER_VE_CHECK(SlipClip(p, "a", 5.0, bounds, &err));
        const Clip* a = FindClip(p, "a");
        XER_VE_CHECK(std::abs(a->in_point - 25.0) < 1e-9);
        XER_VE_CHECK(std::abs(a->timeline_start - 10.0) < 1e-9);
        XER_VE_CHECK(std::abs(a->duration - 8.0) < 1e-9);
        // below zero / past the media end rejected, clip untouched
        XER_VE_CHECK(!SlipClip(p, "a", -30.0, bounds, &err));
        XER_VE_CHECK(!SlipClip(p, "a", 80.0, bounds, &err));  // 105 > 100
        XER_VE_CHECK(std::abs(a->in_point - 25.0) < 1e-9);
        // rate-aware bound: rate 2 consumes 16 s inside an 8 s clip
        Clip fast = MakeClip("f", 0, 50.0, 60.0, 8.0);
        fast.playback_rate = 2.0;
        XER_VE_CHECK(AddClip(p, fast, bounds, &err));
        XER_VE_CHECK(SlipClip(p, "f", 24.0, bounds, &err));  // 84+16=100
        XER_VE_CHECK(!SlipClip(p, "f", 17.0, bounds, &err)); // 117 > 100
    }

    // -- SlideClip --------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("m", 0, 10.0, 40.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("n", 0, 15.0, 10.0, 10.0), bounds, &err);
        // slide m +2: a's out-point absorbs the vacated head (grows to
        // 12), n's head trims 2 s — no gap, m's in-point unchanged
        XER_VE_CHECK(SlideClip(p, "m", 2.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 12.0) < 1e-9);
        const Clip* m = FindClip(p, "m");
        XER_VE_CHECK(std::abs(m->timeline_start - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(m->in_point - 40.0) < 1e-9);
        const Clip* n = FindClip(p, "n");
        XER_VE_CHECK(std::abs(n->in_point - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(n->timeline_start - 17.0) < 1e-9);
        XER_VE_CHECK(std::abs(n->duration - 8.0) < 1e-9);
        // slide m -4: a shrinks back to 8, n regains 4 s of head
        XER_VE_CHECK(SlideClip(p, "m", -4.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 8.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "m")->timeline_start - 8.0) <
                     1e-9);
        n = FindClip(p, "n");
        XER_VE_CHECK(std::abs(n->in_point - 8.0) < 1e-9);
        XER_VE_CHECK(std::abs(n->timeline_start - 13.0) < 1e-9);
        XER_VE_CHECK(std::abs(n->duration - 12.0) < 1e-9);
        // too far left collapses a → whole edit rejected (all three
        // clips unchanged)
        XER_VE_CHECK(!SlideClip(p, "m", -10.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 8.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "m")->timeline_start - 8.0) <
                     1e-9);
        // too far right collapses n → rejected
        XER_VE_CHECK(!SlideClip(p, "m", 20.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "n")->duration - 12.0) < 1e-9);
    }

    // -- SlideClip across a gap degrades to a bounded move ---------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("m", 0, 20.0, 0.0, 5.0), bounds, &err);
        // no adjacent neighbours → nothing absorbs; m just moves
        XER_VE_CHECK(SlideClip(p, "m", -5.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "m")->timeline_start - 15.0) <
                     1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 5.0) < 1e-9);
        // flush against a is allowed (adjacency, no overlap)
        XER_VE_CHECK(SlideClip(p, "m", -10.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "m")->timeline_start - 5.0) <
                     1e-9);
        // and now that m is flush, sliding right stretches a's tail
        XER_VE_CHECK(SlideClip(p, "m", 3.0, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 8.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "m")->timeline_start - 8.0) <
                     1e-9);
    }

    // -- SetClipRate ------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 10.0, 20.0, 8.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 30.0, 0.0, 5.0), bounds, &err);
        // 2x: the same 8 s of source plays in 4 s of timeline
        XER_VE_CHECK(SetClipRate(p, "a", 2.0, bounds, &err));
        const Clip* a = FindClip(p, "a");
        XER_VE_CHECK(std::abs(a->playback_rate - 2.0) < 1e-9);
        XER_VE_CHECK(std::abs(a->duration - 4.0) < 1e-9);
        XER_VE_CHECK(std::abs(a->in_point - 20.0) < 1e-9);
        // 0.5x: consumed span (4 * 2 = 8 s) now takes 16 s
        XER_VE_CHECK(SetClipRate(p, "a", 0.5, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 16.0) < 1e-9);
        // 0.4x: ends exactly at b's start — adjacency is fine
        XER_VE_CHECK(SetClipRate(p, "a", 0.4, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 20.0) < 1e-9);
        // 0.2x would grow the clip INTO b → rejected, rate/dur unchanged
        XER_VE_CHECK(!SetClipRate(p, "a", 0.2, bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 20.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->playback_rate - 0.4) <
                     1e-9);
        // non-positive / non-finite rates rejected
        XER_VE_CHECK(!SetClipRate(p, "a", 0.0, bounds, &err));
        XER_VE_CHECK(!SetClipRate(p, "a", -1.0, bounds, &err));
        XER_VE_CHECK(!SetClipRate(
            p, "a", std::numeric_limits<double>::quiet_NaN(), bounds,
            &err));
    }

    // -- InsertClipAt -----------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 5.0), bounds, &err);
        XER_VE_CHECK(InsertClipAt(p, MakeClip("x", 0, 5.0, 0.0, 3.0),
                                  bounds, &err));
        // everything at/after the insert point shifted right by 3
        XER_VE_CHECK(std::abs(FindClip(p, "b")->timeline_start - 8.0) <
                     1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 23.0) <
                     1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "x")->timeline_start - 5.0) <
                     1e-9);
        // inserting inside a clip (spanner at 9 inside b [8,13)) is
        // rejected and the shift is rolled back
        XER_VE_CHECK(!InsertClipAt(p, MakeClip("y", 0, 9.0, 0.0, 4.0),
                                   bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "b")->timeline_start - 8.0) <
                     1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 23.0) <
                     1e-9);
        XER_VE_CHECK(FindClip(p, "y") == nullptr);
        // locked track rejects
        p.tracks[0].locked = true;
        XER_VE_CHECK(!InsertClipAt(p, MakeClip("z", 0, 30.0, 0.0, 4.0),
                                   bounds, &err));
        p.tracks[0].locked = false;
    }

    // -- OverwriteClipAt --------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 10.0), bounds, &err);
        AddClip(p, MakeClip("c", 0, 20.0, 0.0, 10.0), bounds, &err);
        // overwrite [8,22): a trimmed to [0,8), b consumed, c's tail
        // [22,30) survives under a new id — same splits as LiftRange.
        XER_VE_CHECK(OverwriteClipAt(p, MakeClip("x", 0, 8.0, 0.0, 14.0),
                                     bounds, &err));
        XER_VE_CHECK(p.clips.size() == 3);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->duration - 8.0) < 1e-9);
        XER_VE_CHECK(FindClip(p, "b") == nullptr);
        XER_VE_CHECK(FindClip(p, "c") == nullptr);
        XER_VE_CHECK(FindClip(p, "x") != nullptr);
        const Clip* tail = nullptr;
        for (const auto& cl : p.clips) {
            if (cl.id != "a" && cl.id != "x") {
                tail = &cl;
            }
        }
        XER_VE_CHECK(tail != nullptr);
        XER_VE_CHECK(std::abs(tail->timeline_start - 22.0) < 1e-9);
        XER_VE_CHECK(std::abs(tail->duration - 8.0) < 1e-9);
        XER_VE_CHECK(std::abs(tail->in_point - 2.0) < 1e-9);
        // invalid clip → nothing cleared, nothing added
        const std::size_t n = p.clips.size();
        XER_VE_CHECK(!OverwriteClipAt(
            p, MakeClip("bad", 0, 0.0, 0.0, -5.0), bounds, &err));
        XER_VE_CHECK(p.clips.size() == n);
        XER_VE_CHECK(FindClip(p, "a") != nullptr);
    }

    // -- markers ----------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        Marker m1;
        m1.time = 5.0;
        m1.name = "five";
        Marker m2;
        m2.time = 2.0;
        m2.name = "two";
        XER_VE_CHECK(AddMarker(p, m1, &err));
        XER_VE_CHECK(AddMarker(p, m2, &err));
        XER_VE_CHECK(p.markers.size() == 2);
        XER_VE_CHECK(p.markers[0].time == 2.0);   // sorted by time
        XER_VE_CHECK(p.markers[1].time == 5.0);
        // equal time merges (updates name/color, no duplicate)
        Marker m3;
        m3.time = 5.0;
        m3.name = "five-updated";
        XER_VE_CHECK(AddMarker(p, m3, &err));
        XER_VE_CHECK(p.markers.size() == 2);
        XER_VE_CHECK(p.markers[1].name == "five-updated");
        XER_VE_CHECK(RemoveMarker(p, 5.0));
        XER_VE_CHECK(p.markers.size() == 1);
        XER_VE_CHECK(!RemoveMarker(p, 99.0));
        Marker bad;
        bad.time = -1.0;
        XER_VE_CHECK(!AddMarker(p, bad, &err));
        // markers feed the snap points
        const auto pts = CollectSnapPoints(p, -1, -1.0);
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 2.0) !=
                     pts.end());
    }

    // -- work area --------------------------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        // out beyond the project clamps to its duration
        XER_VE_CHECK(SetWorkArea(p, 2.0, 60.0, &err));
        XER_VE_CHECK(p.work_in == 2.0 && p.work_out == 10.0);
        // out <= in resets to "whole project"
        XER_VE_CHECK(SetWorkArea(p, 4.0, 1.0, &err));
        XER_VE_CHECK(p.work_out <= p.work_in);
        // work-area edges are snap points while the area is set
        XER_VE_CHECK(SetWorkArea(p, 2.0, 8.0, &err));
        const auto pts = CollectSnapPoints(p, -1, -1.0);
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 2.0) !=
                     pts.end());
        XER_VE_CHECK(std::find(pts.begin(), pts.end(), 8.0) !=
                     pts.end());
        // negative / NaN rejected
        XER_VE_CHECK(!SetWorkArea(p, -1.0, 5.0, &err));
        XER_VE_CHECK(!SetWorkArea(
            p, 0.0, std::numeric_limits<double>::quiet_NaN(), &err));
    }

    // -- track lock rejects clip-affecting ops ----------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), bounds, &err);
        XER_VE_CHECK(SetTrackLocked(p, 0, true, &err));
        XER_VE_CHECK(!MoveClip(p, "a", 20.0, bounds, &err));
        XER_VE_CHECK(!TrimClip(p, "a", TrimEdge::Out, 4.0, bounds, &err));
        XER_VE_CHECK(!SplitClip(p, "a", 2.0, "s", &err));
        XER_VE_CHECK(!RemoveClip(p, "a"));
        XER_VE_CHECK(!AddClip(p, MakeClip("x", 0, 20.0, 0.0, 5.0),
                              bounds, &err));
        XER_VE_CHECK(p.clips.size() == 2);
        XER_VE_CHECK(SetTrackLocked(p, 0, false, &err));
        XER_VE_CHECK(MoveClip(p, "a", 20.0, bounds, &err));
        XER_VE_CHECK(!SetTrackLocked(p, 9, true, &err));   // bad index
    }

    // -- link group + clip keyframes --------------------------------------
    {
        Project p = MakeProject();
        std::string err;
        MediaBounds bounds{100.0, true};
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 10.0), bounds, &err);
        XER_VE_CHECK(SetClipLinkGroup(p, "a", "grp1"));
        XER_VE_CHECK(FindClip(p, "a")->link_group == "grp1");
        XER_VE_CHECK(SetClipLinkGroup(p, "a", ""));   // unlink
        XER_VE_CHECK(FindClip(p, "a")->link_group.empty());
        XER_VE_CHECK(!SetClipLinkGroup(p, "zzz", "g"));
        // keyframes are stored sorted; keys must sit in [0, duration]
        std::vector<Keyframe> keys = {
            {8.0, 1.0f, KeyEase::Linear},
            {2.0, 0.0f, KeyEase::Linear},
        };
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Opacity, keys,
                                      &err));
        const auto& stored =
            FindClip(p, "a")->keyframes.at(KeyProp::Opacity);
        XER_VE_CHECK(stored.size() == 2 && stored[0].time == 2.0);
        std::vector<Keyframe> bad = {{20.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(!SetClipKeyframes(p, "a", KeyProp::Opacity, bad,
                                       &err));
        // an empty key list removes the property
        XER_VE_CHECK(SetClipKeyframes(p, "a", KeyProp::Opacity, {},
                                      &err));
        XER_VE_CHECK(FindClip(p, "a")->keyframes.empty());
    }

    // -- keyframe evaluation (easing math) --------------------------------
    {
        Clip c = MakeClip("k", 0, 0.0, 0.0, 10.0);
        // Linear midpoint interpolates
        c.keyframes[KeyProp::Opacity] = {
            {0.0, 0.0f, KeyEase::Linear}, {10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 5.0, 0.7f) - 0.5f) < 1e-6);
        // Hold keeps the previous value until the next key
        c.keyframes[KeyProp::Opacity] = {
            {0.0, 0.3f, KeyEase::Hold}, {10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 5.0, 0.7f) - 0.3f) < 1e-6);
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 10.0, 0.7f) - 1.0f) < 1e-6);
        // EaseIn/Out midpoints: quadratic 0.25 / 0.75; EaseInOut is
        // symmetric smoothstep → 0.5 at the midpoint
        c.keyframes[KeyProp::Opacity] = {
            {0.0, 0.0f, KeyEase::EaseIn}, {10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 5.0, 0.7f) - 0.25f) <
                     1e-6);
        c.keyframes[KeyProp::Opacity] = {
            {0.0, 0.0f, KeyEase::EaseOut},
            {10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 5.0, 0.7f) - 0.75f) <
                     1e-6);
        c.keyframes[KeyProp::Opacity] = {
            {0.0, 0.0f, KeyEase::EaseInOut},
            {10.0, 1.0f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 5.0, 0.7f) - 0.5f) < 1e-6);
        // no keys → fallback; outside the key range → endpoint value
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Scale, 5.0, 0.7f) - 0.7f) < 1e-6);
        c.keyframes[KeyProp::Opacity] = {
            {2.0, 0.4f, KeyEase::Linear}, {8.0, 0.9f, KeyEase::Linear}};
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 0.0, 0.7f) - 0.4f) < 1e-6);
        XER_VE_CHECK(std::fabs(EvaluateClipProperty(
                         c, KeyProp::Opacity, 9.0, 0.7f) - 0.9f) < 1e-6);
        // EffectiveTransformAt overrides only the animated property
        c.transform.opacity = 0.5f;
        c.transform.scale = 2.0f;
        const Transform t = EffectiveTransformAt(c, 5.0);
        XER_VE_CHECK(std::fabs(t.opacity - 0.65f) < 1e-6);   // mid 0.4→0.9
        XER_VE_CHECK(std::fabs(t.scale - 2.0f) < 1e-6);
        c.keyframes[KeyProp::GainDb] = {{0.0, -6.0f, KeyEase::Hold}};
        XER_VE_CHECK(std::fabs(EffectiveGainDbAt(c, 5.0) - -6.0f) <
                     1e-6);
    }
    return 0;
}
