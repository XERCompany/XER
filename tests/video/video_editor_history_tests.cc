// Undo/redo suite for TimelineHistory — every case drives mutations
// through XER::VideoEditor::TimelineHistory (never the raw model ops)
// and compares full Project state where practical.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "timeline_history.h"

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

bool NearlyEq(double a, double b) { return std::fabs(a - b) <= 1e-9; }
bool NearlyEq(float a, float b) {
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= 1e-6;
}

// Field-by-field equality with tolerance on floating point — used to
// prove undo/redo restores the EXACT prior state (clip vector order,
// track table, and the full transition set included).
bool ClipsEqual(const Clip& a, const Clip& b) {
    if (a.id != b.id || a.source_path != b.source_path ||
        a.track != b.track || a.muted != b.muted) {
        return false;
    }
    if (!NearlyEq(a.timeline_start, b.timeline_start) ||
        !NearlyEq(a.in_point, b.in_point) ||
        !NearlyEq(a.duration, b.duration) ||
        !NearlyEq(a.gain_db, b.gain_db) ||
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
    if (a.effects.size() != b.effects.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.effects.size(); ++i) {
        if (a.effects[i].type != b.effects[i].type ||
            a.effects[i].params.size() != b.effects[i].params.size()) {
            return false;
        }
        for (std::size_t j = 0; j < a.effects[i].params.size(); ++j) {
            if (!NearlyEq(a.effects[i].params[j], b.effects[i].params[j])) {
                return false;
            }
        }
    }
    return true;
}

// Prints the first differing section to stderr, then returns false —
// keeps XER_VE_CHECK output useful when a full-state compare fails.
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
            x.hidden != y.hidden || !NearlyEq(x.gain_db, y.gain_db)) {
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
    return true;
}

}  // namespace

int XER_VideoEditor_RunHistoryTests() {
    const MediaBounds video_bounds{100.0, true};
    const MediaBounds audio_bounds{100.0, false};

    // -- 1. AddClip undo/redo + label ---------------------------------
    {
        Project p = MakeProject();
        TimelineHistory history(p);
        const Project empty = p;
        std::string id, err;
        Clip clip = MakeClip("", 0, 0.0, 0.0, 5.0);
        clip.gain_db = -3.0f;
        XER_VE_CHECK(history.AddClip(clip, video_bounds, &id, &err));
        XER_VE_CHECK(!id.empty());                  // wrapper filled the id
        XER_VE_CHECK(FindClip(p, id) != nullptr);
        XER_VE_CHECK(history.CanUndo());
        XER_VE_CHECK(history.UndoLabel() == "Add clip");
        XER_VE_CHECK(!history.CanRedo());
        const Project added = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, empty));
        XER_VE_CHECK(!history.CanUndo());
        XER_VE_CHECK(history.CanRedo());
        XER_VE_CHECK(history.RedoLabel() == "Add clip");
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, added));      // same generated id
        XER_VE_CHECK(FindClip(p, id) != nullptr);
    }

    // -- 2. MoveClip restores position AND the transition it dropped ---
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("x", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("a", 0, 5.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("y", 0, 10.0, 0.0, 5.0), video_bounds, &err);
        XER_VE_CHECK(AddTransition(p, "x", "a",
                                   Transition::Type::Crossfade, 1.0, &err));
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.MoveClip("a", 20.0, video_bounds, &err));
        XER_VE_CHECK(p.transitions.empty());        // x->a dropped by the move
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 20.0) < 1e-9);
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));     // clip AND transition back
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(p.transitions.empty());
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 20.0) < 1e-9);
    }

    // -- 3. TrimClip undo restores in/out/duration/start ----------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 10.0, 20.0, 8.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.TrimClip("a", TrimEdge::In, 22.0,
                                      video_bounds, &err));
        const Clip* c = FindClip(p, "a");
        XER_VE_CHECK(c != nullptr);
        XER_VE_CHECK(std::abs(c->in_point - 22.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->timeline_start - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->duration - 6.0) < 1e-9);
        const Project trimmed = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, trimmed));
    }

    // -- 4. SplitClip: new id via out-param, deterministic redo ----------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("x", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("a", 0, 5.0, 0.0, 10.0), video_bounds, &err);
        AddClip(p, MakeClip("y", 0, 15.0, 0.0, 5.0), video_bounds, &err);
        AddTransition(p, "x", "a", Transition::Type::Crossfade, 1.0, &err);
        AddTransition(p, "a", "y", Transition::Type::WipeLeft, 1.0, &err);
        TimelineHistory history(p);
        const Project before = p;
        std::string new_id;
        XER_VE_CHECK(history.SplitClip("a", 10.0, &new_id, &err));
        XER_VE_CHECK(!new_id.empty());
        XER_VE_CHECK(p.clips.size() == 4);
        const Project split = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));    // one "a", BOTH transitions
        bool saw_xa = false, saw_ay = false;
        for (const auto& t : p.transitions) {
            saw_xa |= (t.from_clip == "x" && t.to_clip == "a");
            saw_ay |= (t.from_clip == "a" && t.to_clip == "y");
        }
        XER_VE_CHECK(saw_xa && saw_ay);
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, split));     // same new id — redo stable
        XER_VE_CHECK(FindClip(p, new_id) != nullptr);
        XER_VE_CHECK(p.transitions.size() == 2);
    }

    // -- 5. Macro groups many edits into ONE undo step; nesting merges --
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("c", 0, 30.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project before_macro = p;
        history.BeginMacro("drag");
        XER_VE_CHECK(history.MoveClip("a", 16.0, video_bounds, &err));
        XER_VE_CHECK(history.MoveClip("a", 22.0, video_bounds, &err));
        XER_VE_CHECK(history.MoveClip("a", 40.0, video_bounds, &err));
        history.EndMacro();
        XER_VE_CHECK(history.CanUndo());
        XER_VE_CHECK(history.UndoLabel() == "drag");
        XER_VE_CHECK(history.Undo());               // single step → pre-macro
        XER_VE_CHECK(ProjectsEqual(p, before_macro));
        XER_VE_CHECK(history.CanRedo());
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 40.0) < 1e-9);

        // Nested BeginMacro folds into the outer macro — still one step.
        const Project before_nested = p;
        history.BeginMacro("outer");
        XER_VE_CHECK(history.MoveClip("a", 50.0, video_bounds, &err));
        history.BeginMacro("inner");
        XER_VE_CHECK(history.MoveClip("b", 20.0, video_bounds, &err));
        history.EndMacro();
        XER_VE_CHECK(history.MoveClip("c", 60.0, video_bounds, &err));
        history.EndMacro();
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before_nested));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 50.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "b")->timeline_start - 20.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "c")->timeline_start - 60.0) < 1e-9);
        // The earlier macro is still undoable underneath.
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before_nested));
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before_macro));
    }

    // -- 6a. Consecutive MoveClip on the SAME clip coalesce --------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 30.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project original = p;
        XER_VE_CHECK(history.MoveClip("a", 5.0, video_bounds, &err));
        XER_VE_CHECK(history.MoveClip("a", 15.0, video_bounds, &err));
        XER_VE_CHECK(history.MoveClip("a", 22.0, video_bounds, &err));
        // Merged: ONE undo returns to the ORIGINAL position (0), not 15.
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, original));
        XER_VE_CHECK(!history.CanUndo());
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 22.0) < 1e-9);
    }

    // -- 6b. MoveClip on a DIFFERENT clip does NOT merge -----------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 30.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project original = p;
        XER_VE_CHECK(history.MoveClip("a", 5.0, video_bounds, &err));
        XER_VE_CHECK(history.MoveClip("b", 40.0, video_bounds, &err));
        XER_VE_CHECK(history.Undo());               // only b's move reverts
        XER_VE_CHECK(std::abs(FindClip(p, "b")->timeline_start - 30.0) < 1e-9);
        XER_VE_CHECK(std::abs(FindClip(p, "a")->timeline_start - 5.0) < 1e-9);
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, original));
        XER_VE_CHECK(!history.CanUndo());
    }

    // -- 6c. TrimClip merges on same edge, not across edges --------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 10.0, 20.0, 8.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project original = p;                 // in=20 out=28 start=10
        XER_VE_CHECK(history.TrimClip("a", TrimEdge::In, 21.0,
                                      video_bounds, &err));
        XER_VE_CHECK(history.TrimClip("a", TrimEdge::In, 22.0,
                                      video_bounds, &err));
        XER_VE_CHECK(history.Undo());               // merged → original
        XER_VE_CHECK(ProjectsEqual(p, original));
        XER_VE_CHECK(history.Redo());
        const Clip* c = FindClip(p, "a");
        XER_VE_CHECK(std::abs(c->in_point - 22.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->timeline_start - 12.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->duration - 6.0) < 1e-9);
        // A trim on the OTHER edge is a separate undo step.
        XER_VE_CHECK(history.TrimClip("a", TrimEdge::Out, 27.0,
                                      video_bounds, &err));
        XER_VE_CHECK(history.Undo());               // reverts out-trim only
        c = FindClip(p, "a");
        XER_VE_CHECK(std::abs(c->in_point - 22.0) < 1e-9);
        XER_VE_CHECK(std::abs(c->duration - 6.0) < 1e-9);
        XER_VE_CHECK(history.Undo());               // merged in-trims → orig
        XER_VE_CHECK(ProjectsEqual(p, original));
    }

    // -- 7. ReplaceClip (inspector edit) undo/redo ------------------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project before = p;
        Clip repl = *FindClip(p, "a");
        repl.gain_db = -6.0f;
        repl.muted = true;
        XER_VE_CHECK(history.ReplaceClip("a", repl, video_bounds, &err));
        XER_VE_CHECK(std::abs(FindClip(p, "a")->gain_db - -6.0f) < 1e-6);
        XER_VE_CHECK(FindClip(p, "a")->muted);
        const Project replaced = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, replaced));
        // A rejected ReplaceClip must not push history.
        Clip bad = *FindClip(p, "a");
        bad.timeline_start = -1.0;
        XER_VE_CHECK(!history.ReplaceClip("a", bad, video_bounds, &err));
        XER_VE_CHECK(history.UndoLabel() == "Replace clip");
        XER_VE_CHECK(ProjectsEqual(p, replaced));
    }

    // -- 8. AddTransition / RemoveTransition undo/redo -------------------
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 5.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        // Bad cut (gap): AddTransition fails and pushes nothing.
        XER_VE_CHECK(MoveClip(p, "b", 7.0, video_bounds, &err));
        const Project gapped = p;
        const bool could_undo = history.CanUndo();
        XER_VE_CHECK(!history.AddTransition("a", "b",
                                            Transition::Type::Crossfade,
                                            1.0, &err));
        XER_VE_CHECK(history.CanUndo() == could_undo);
        XER_VE_CHECK(ProjectsEqual(p, gapped));
        // Restore adjacency (model-level setup), then history add.
        XER_VE_CHECK(MoveClip(p, "b", 5.0, video_bounds, &err));
        const Project adjacent = p;
        XER_VE_CHECK(history.AddTransition("a", "b",
                                           Transition::Type::WipeLeft,
                                           0.5, &err));
        XER_VE_CHECK(p.transitions.size() == 1);
        const Project with_transition = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, adjacent));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, with_transition));
        // RemoveTransition through history restores the exact transition.
        XER_VE_CHECK(history.RemoveTransition("a", "b"));
        XER_VE_CHECK(p.transitions.empty());
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(p.transitions.size() == 1);
        XER_VE_CHECK(p.transitions[0].type == Transition::Type::WipeLeft);
        XER_VE_CHECK(std::abs(p.transitions[0].duration - 0.5) < 1e-9);
        XER_VE_CHECK(ProjectsEqual(p, with_transition));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(p.transitions.empty());
        // Removing a missing pair fails without pushing history.
        const bool could_undo2 = history.CanUndo();
        XER_VE_CHECK(!history.RemoveTransition("a", "zzz"));
        XER_VE_CHECK(history.CanUndo() == could_undo2);
    }

    // -- 9. Depth cap drops the oldest entries ---------------------------
    {
        Project p = MakeProject();
        TimelineHistory history(p, 5);
        Project after_three, all_eight;
        for (int i = 0; i < 8; ++i) {
            std::string id, err;
            XER_VE_CHECK(history.AddClip(
                MakeClip("", 0, i * 10.0, 0.0, 5.0), video_bounds, &id,
                &err));
            if (i == 2) {
                after_three = p;
            }
        }
        all_eight = p;
        // 8 edits, depth 5 → the 3 oldest are gone; 5 undos reach the
        // state after edit #3.
        for (int i = 0; i < 5; ++i) {
            XER_VE_CHECK(history.Undo());
        }
        XER_VE_CHECK(ProjectsEqual(p, after_three));
        XER_VE_CHECK(!history.CanUndo());
        XER_VE_CHECK(history.CanRedo());
        for (int i = 0; i < 5; ++i) {
            XER_VE_CHECK(history.Redo());
        }
        XER_VE_CHECK(ProjectsEqual(p, all_eight));
    }

    // -- 10. Clear() empties both stacks; failed Execute pushes nothing -
    {
        Project p = MakeProject();
        std::string err;
        AddClip(p, MakeClip("a", 0, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("b", 0, 10.0, 0.0, 5.0), video_bounds, &err);
        TimelineHistory history(p);
        const Project settled = p;
        XER_VE_CHECK(!history.MoveClip("a", 12.0, video_bounds, &err));
        XER_VE_CHECK(!history.CanUndo());               // rejected → no push
        XER_VE_CHECK(ProjectsEqual(p, settled));
        XER_VE_CHECK(history.MoveClip("a", 20.0, video_bounds, &err));
        const Project moved = p;
        // A second rejected edit leaves the existing entry untouched.
        XER_VE_CHECK(!history.MoveClip("a", 12.0, video_bounds, &err));
        XER_VE_CHECK(history.UndoLabel() == "Move clip");
        XER_VE_CHECK(ProjectsEqual(p, moved));
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, settled));
        XER_VE_CHECK(history.CanRedo());
        history.Clear();
        XER_VE_CHECK(!history.CanUndo());
        XER_VE_CHECK(!history.CanRedo());
        XER_VE_CHECK(ProjectsEqual(p, settled));        // Clear ≠ revert
    }

    // -- 11. RemoveTrack reindexes clip.track on undo/redo ---------------
    {
        Project p = MakeProject();                     // [V1, A1]
        std::string err;
        XER_VE_CHECK(AddTrack(p, TrackKind::Video, "V2") == 2);
        XER_VE_CHECK(AddTrack(p, TrackKind::Audio, "A2") == 3);
        AddClip(p, MakeClip("v1", 2, 0.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("v2", 2, 5.0, 0.0, 5.0), video_bounds, &err);
        AddClip(p, MakeClip("au", 3, 0.0, 0.0, 5.0, "D:/media/s.mp3"),
                audio_bounds, &err);
        AddTransition(p, "v1", "v2", Transition::Type::Crossfade, 1.0, &err);
        TimelineHistory history(p);
        const Project before = p;
        XER_VE_CHECK(history.RemoveTrack(1, &err));    // A1 is empty
        XER_VE_CHECK(p.tracks.size() == 3);
        XER_VE_CHECK(FindClip(p, "v1")->track == 1);   // 2 → 1
        XER_VE_CHECK(FindClip(p, "v2")->track == 1);
        XER_VE_CHECK(FindClip(p, "au")->track == 2);   // 3 → 2
        XER_VE_CHECK(p.transitions.size() == 1);       // reindex keeps it valid
        const Project removed = p;
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, before));       // tracks + clip.track back
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(ProjectsEqual(p, removed));
        // AddTrack through history is undoable too.
        const Project pre_add = p;
        const int added = history.AddTrack(TrackKind::Audio, "A3");
        XER_VE_CHECK(added == static_cast<int>(p.tracks.size()) - 1);
        XER_VE_CHECK(p.tracks.size() == 4);
        XER_VE_CHECK(history.Undo());
        XER_VE_CHECK(ProjectsEqual(p, pre_add));
        XER_VE_CHECK(history.Redo());
        XER_VE_CHECK(p.tracks.size() == 4);
    }

    return 0;
}
