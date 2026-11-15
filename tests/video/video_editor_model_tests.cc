#include <algorithm>
#include <cmath>

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
    return 0;
}
