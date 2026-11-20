// Source-edit suite for the Premiere-style source monitor
// (source_edit.h/.cc): the headless half of the three-point-edit
// path. Covers SourceMarks -> ResolveSourceRange clamping/fallbacks,
// MakeSourceClip field stamping, and SourceEditAt dispatch to the
// model's InsertClipAt / OverwriteClipAt — including the required
// "marked range -> inserted clip with the correct source range and
// duration" end-to-end check plus the ripple/overwrite placement
// semantics and transactional rejection of an inverted range.
#include <cmath>
#include <string>

#include "source_edit.h"
#include "timeline_model.h"
#include "video_editor_test_util.h"

namespace {
using namespace XER::VideoEditor;

bool NearlyEq(double a, double b) { return std::fabs(a - b) <= 1e-9; }

Project MakeProjectWithClip() {
    Project project;
    project.width = 1920;
    project.height = 1080;
    project.fps_num = 30;
    project.fps_den = 1;
    AddTrack(project, TrackKind::Video, "V1");
    Clip base;
    base.id = "base";
    base.source_path = "a.mp4";
    base.track = 0;
    base.timeline_start = 0.0;
    base.duration = 6.0;
    const MediaBounds media{60.0, true};
    std::string err;
    const bool added = AddClip(project, base, media, &err);
    (void)added;
    return project;
}

void TestResolveSourceRange() {
    double in = -1.0, out = -1.0;

    // Both marks set: verbatim within the media bounds.
    SourceMarks marks;
    marks.in = 2.0;
    marks.out = 5.5;
    XER_VE_CHECK(ResolveSourceRange(marks, 20.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 2.0) && NearlyEq(out, 5.5));

    // Unmarked sides resolve against the media bounds.
    marks = {};
    marks.in = 3.0;
    XER_VE_CHECK(ResolveSourceRange(marks, 10.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 3.0) && NearlyEq(out, 10.0));
    marks = {};
    marks.out = 4.0;
    XER_VE_CHECK(ResolveSourceRange(marks, 10.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 0.0) && NearlyEq(out, 4.0));

    // No marks at all: the whole clip.
    XER_VE_CHECK(ResolveSourceRange(SourceMarks{}, 7.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 0.0) && NearlyEq(out, 7.0));

    // Marks past the probed duration clamp like Premiere.
    marks = {};
    marks.in = 18.0;
    marks.out = 25.0;
    XER_VE_CHECK(ResolveSourceRange(marks, 20.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 18.0) && NearlyEq(out, 20.0));

    // Unknown duration + no out mark falls back to the 5 s default
    // span from the in mark (same convention as AddMediaToTimeline).
    marks = {};
    marks.in = 2.0;
    XER_VE_CHECK(ResolveSourceRange(marks, 0.0, &in, &out));
    XER_VE_CHECK(NearlyEq(in, 2.0) &&
                 NearlyEq(out, 2.0 + kSourceFallbackSeconds));

    // Inverted / empty ranges must be rejected, never placed.
    marks = {};
    marks.in = 5.0;
    marks.out = 5.0;
    XER_VE_CHECK(!ResolveSourceRange(marks, 20.0, &in, &out));
    marks.out = 4.0;
    XER_VE_CHECK(!ResolveSourceRange(marks, 20.0, &in, &out));

    // Null out-params reject cleanly.
    marks = {};
    XER_VE_CHECK(!ResolveSourceRange(marks, 20.0, nullptr, &out));
    XER_VE_CHECK(!ResolveSourceRange(marks, 20.0, &in, nullptr));
}

void TestMakeSourceClip() {
    const Clip clip = MakeSourceClip("b.mp4", 0, 4.0, 2.0, 5.5,
                                     3840, 2160, 1920, 1080);
    XER_VE_CHECK(clip.source_path == "b.mp4");
    XER_VE_CHECK(clip.track == 0);
    XER_VE_CHECK(NearlyEq(clip.timeline_start, 4.0));
    XER_VE_CHECK(NearlyEq(clip.in_point, 2.0));
    XER_VE_CHECK(NearlyEq(clip.duration, 3.5));
    XER_VE_CHECK(NearlyEq(clip.playback_rate, 1.0));
    // 4K source on a 1080p canvas gets a fit transform (scale ~0.5).
    XER_VE_CHECK(NearlyEq(clip.transform.scale, 0.5));

    // Audio-only media (no dims) keeps the identity transform.
    const Clip audio = MakeSourceClip("c.wav", 1, 0.0, 0.0, 4.0,
                                      0, 0, 1920, 1080);
    XER_VE_CHECK(NearlyEq(audio.transform.scale, 1.0));
    XER_VE_CHECK(NearlyEq(audio.transform.offset_x, 0.0));
}

void TestInsertPreservesMarkedRange() {
    // The required end-to-end check: marks on the source resolve to a
    // clip that lands on the timeline with the same in_point and
    // duration — plus the ripple shift on following clips.
    Project project = MakeProjectWithClip();      // "base" [0,6)
    Clip tail;
    tail.id = "tail";
    tail.source_path = "a.mp4";
    tail.track = 0;
    tail.timeline_start = 6.0;
    tail.duration = 4.0;
    const MediaBounds media{60.0, true};
    std::string err;
    XER_VE_CHECK(AddClip(project, tail, media, &err));

    SourceMarks marks;
    marks.in = 2.0;
    marks.out = 5.5;
    double src_in = 0.0, src_out = 0.0;
    XER_VE_CHECK(ResolveSourceRange(marks, 20.0, &src_in, &src_out));

    // Insert at the cut point t=6: the ripple pushes "tail" right by
    // the marked duration (3.5 s).
    Clip edit = MakeSourceClip("b.mp4", 0, 6.0, src_in, src_out,
                               3840, 2160, 1920, 1080);
    edit.id = "src-edit";
    XER_VE_CHECK(SourceEditAt(project, edit, media, true, &err));

    const Clip* placed = FindClip(project, "src-edit");
    XER_VE_CHECK(placed != nullptr);
    XER_VE_CHECK(placed != nullptr && NearlyEq(placed->in_point, 2.0));
    XER_VE_CHECK(placed != nullptr && NearlyEq(placed->duration, 3.5));
    XER_VE_CHECK(placed != nullptr &&
                 NearlyEq(placed->timeline_start, 6.0));
    const Clip* shifted = FindClip(project, "tail");
    XER_VE_CHECK(shifted != nullptr &&
                 NearlyEq(shifted->timeline_start, 9.5));
    XER_VE_CHECK(NearlyEq(project.duration(), 13.5));
}

void TestInsertRippleShiftsFollowing() {
    Project project = MakeProjectWithClip();      // "base" [0,6)
    const MediaBounds media{60.0, true};
    std::string err;
    Clip edit = MakeSourceClip("b.mp4", 0, 6.0, 1.0, 4.0,
                               640, 480, 1920, 1080);
    edit.id = "ins";
    // Insert at the base clip's tail: base [0,6) stays, the new clip
    // lands at [6,9) — project duration grows by 3.
    XER_VE_CHECK(SourceEditAt(project, edit, media, true, &err));
    XER_VE_CHECK(project.clips.size() == 2);
    XER_VE_CHECK(NearlyEq(project.duration(), 9.0));
    const Clip* placed = FindClip(project, "ins");
    XER_VE_CHECK(placed != nullptr && NearlyEq(placed->in_point, 1.0) &&
                 NearlyEq(placed->duration, 3.0));
}

void TestInsertInsideStraddlerRejected() {
    // InsertClipAt does not split straddling clips (the app does that
    // explicitly before dispatching) — an insert inside an existing
    // clip's span is rejected and leaves the project untouched.
    Project project = MakeProjectWithClip();      // "base" [0,6)
    const MediaBounds media{60.0, true};
    std::string err;
    Clip edit = MakeSourceClip("b.mp4", 0, 3.0, 0.0, 2.0,
                               640, 480, 1920, 1080);
    edit.id = "ins";
    XER_VE_CHECK(!SourceEditAt(project, edit, media, true, &err));
    XER_VE_CHECK(project.clips.size() == 1);
    XER_VE_CHECK(FindClip(project, "ins") == nullptr);
}

void TestOverwriteReplacesSpan() {
    Project project = MakeProjectWithClip();      // "base" [0,6)
    const MediaBounds media{60.0, true};
    std::string err;
    Clip edit = MakeSourceClip("b.mp4", 0, 2.0, 3.0, 6.0,
                               640, 480, 1920, 1080);
    edit.id = "ovr";
    XER_VE_CHECK(SourceEditAt(project, edit, media, false, &err));
    const Clip* placed = FindClip(project, "ovr");
    XER_VE_CHECK(placed != nullptr);
    XER_VE_CHECK(placed != nullptr && NearlyEq(placed->in_point, 3.0) &&
                 NearlyEq(placed->duration, 3.0) &&
                 NearlyEq(placed->timeline_start, 2.0));
    // The overwritten span is carved out of "base" — the remainder
    // (head [0,2) + tail [5,6)) survives as clips.
    XER_VE_CHECK(project.clips.size() >= 2);
    XER_VE_CHECK(NearlyEq(project.duration(), 6.0));
}

void TestOverwriteAudioOnlyOnAudioTrack() {
    Project project = MakeProjectWithClip();      // has V1 only
    AddTrack(project, TrackKind::Audio, "A1");
    const MediaBounds audio_media{30.0, false};
    std::string err;
    Clip edit = MakeSourceClip("voice.wav", 1, 0.0, 0.0, 5.0,
                               0, 0, 1920, 1080);
    edit.id = "aud";
    XER_VE_CHECK(SourceEditAt(project, edit, audio_media, false, &err));
    const Clip* placed = FindClip(project, "aud");
    XER_VE_CHECK(placed != nullptr && placed->track == 1 &&
                 NearlyEq(placed->duration, 5.0));
}

}  // namespace

int XER_VideoEditor_RunSourceEditTests() {
    TestResolveSourceRange();
    TestMakeSourceClip();
    TestInsertPreservesMarkedRange();
    TestInsertRippleShiftsFollowing();
    TestInsertInsideStraddlerRejected();
    TestOverwriteReplacesSpan();
    TestOverwriteAudioOnlyOnAudioTrack();
    return XER::VideoEditor::Test::Failures();
}
