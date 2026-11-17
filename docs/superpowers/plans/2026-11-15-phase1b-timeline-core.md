# Phase 1B — Timeline Core (Headless) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The headless, UI-free core of the VideoEditor — timeline data
model with enforced invariants, undo/redo history, `.xerproj` JSON
persistence, SQLite media catalog, and clip-poster extraction — all
under `tools/video_editor/` as a static lib (`XER_VideoEditor_Core`)
with plain-assert unit tests runnable under ctest.
**Architecture:** Five modules in `XER::VideoEditor` namespace:
(1) `timeline_model` — spec data types (`TrackKind`, `Transform`,
`ClipEffect`, `Clip`, `Transition`, `Track`, `Project`) plus free-function
queries/mutations that enforce the spec invariants (no overlap, trim
clamps, audio-only→audio-track, transition adjacency/drop);
(2) `timeline_history` — `Command{Apply/Revert/Label/MergeWith}` +
`TimelineHistory{Execute/Undo/Redo/BeginMacro/EndMacro}` mirroring
`XER::NodeGraph::History`, depth 50, every mutation via commands;
(3) `project_store` — static JSON v1 save/load via **jsoncpp_lib**
(decided: guaranteed transitive via `XER_Helper`, root
`find_package(jsoncpp CONFIG REQUIRED)`); (4) `media_catalog` —
`XER::SQL::Storage::SQLiteDatabase` at
`%LOCALAPPDATA%/XER/VideoEditor/catalog.db`, probe via
`XER::Content::Video::ProbeVideo` (relaxed for audio-only in Task 5),
poster paths, UTF-8/case-insensitive path dedup; (5) `clip_poster` —
`VideoDecoder` seek-to-mid + bounded forward-discard +
`video::FrameExporter::ToFile` (PNG).
**Tech Stack:** C++23, CMake + Ninja (preset `default`, vcpkg
`x64-windows-release`, MSVC), jsoncpp, SQLite via `XER_SQL`, FFmpeg
bridge headers only, plain-assert test style.
**Spec:** docs/superpowers/specs/2026-11-15-video-editor-design.md
(data model ~L158-246, JSON ~L250-267, catalog ~L270-287, undo ~L418-425);
plan index 1B block: docs/superpowers/plans/2026-11-15-phase1-plan-index.md
**Baseline:** Phase-1A merged to main at `a0f5058`.

## Global Constraints

- **MSVC env required for every cmake/ctest invocation** — plain shells
  fail `C1083` on standard headers. Use the repo helper:
  `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target <t>`
  and `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure`.
  (Re-configure after CMake edits: `"D:/Developers/XER3/out/vcenv.bat" cmake --preset default`.)
- Build dir `out/build/default` (Ninja preset `default`); test/tool exes
  land in `out/build/default/bin` via the global
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY`.
- **C++23**, repo style: snake_case files, camelCase/PascalCase code,
  tabs, `#pragma once`, RAII, no exceptions in hot paths, contextual
  errors via `std::string* error_out` (nullptr-tolerant).
- **Namespace `XER::VideoEditor`** for all new code; sources live in
  `tools/video_editor/` (flat, like `tools/preview`).
- **No FFmpeg headers outside `src/wrapper/ffmpeg`** — editor code
  includes only `wrapper/ffmpeg/ffmpeg_video_decoder.h` /
  `ffmpeg_engine_bridge.h`. Tests synthesize media at runtime in
  `%TEMP%` (repo pattern) and self-skip FFmpeg checks via
  `XER::Bridge::FFmpeg::IsAvailable()`.
- **JSON lib is `jsoncpp_lib`** — decided (plan index + build infra
  wiki): `find_package(jsoncpp CONFIG REQUIRED)` at root `CMakeLists.txt:67`,
  PUBLIC-linked by `XER_Helper`; include `<json/json.h>`. Do NOT use
  nlohmann (only wired inside `src/content/ai`).
- **Paths are UTF-8 `std::string`** at every XER boundary. Convert at
  filesystem edges with `XER::Helper::Path::{NormalizeUtf8Path,FromUtf8,
  ToUtf8}` (`src/helper/path_utils.h`) and env vars with
  `XER::Helper::GetEnv` (`src/helper/env_utils.h`) + `NormalizeUtf8Path`
  (GetEnv returns ANSI on Windows — NormalizeUtf8Path repairs it).
- **SQLite**: `XER::SQL::Storage::SQLiteDatabase` (`src/sql/storage/
  sqlite_database.h`) — `Execute(sql, params)` runs ONE statement only
  (multi-statement scripts silently truncate — see
  `node_graph_project_store.cc:37-39`); bind via
  `XER::SQL::Content::Value` (variant<int64,double,string,Blob>).
  `node_graph_project_store.{h,cc}` is the reference implementation.
- **1A decoder contract** (binding): `VideoDecoder::Open` accepts
  audio-only containers (`GetWidth/Height()==0`, `HasAudio()==true`);
  audio-only `NextFrame()` pumps ≤256 packets/call and returns false —
  consumers alternate `NextFrame()`/`ReadAudioFrames()` until
  `AudioDrained()`. `Seek()` on audio-only has a known timestamp-units
  bug (`av_rescale_q` needed) — **1C concern, not 1B** (posters skip
  audio-only files).
- The timeline model is **UI-thread-owned, synchronous, pure** — no
  threads, no async, no engine objects inside `Project`. Media bounds
  (duration/has_video) are passed in per call via `MediaBounds`,
  resolved by callers from `MediaCatalog`/`ProbeVideo`.
- Do **not** touch `src/video` renderers, `src/wrapper/ffmpeg` encode
  paths, or mock bridge functions (`RunFilterGraph`, `Transcode`,
  `CaptureDeviceToStream`).

## Review Focus

The five input classes / failure modes most likely to bite:

1. **JSON round-trip fidelity** (`.xerproj`): float `Transform`/
   `ClipEffect.params`/`gain_db` must survive double→float→double;
   UTF-8 (e.g. Cyrillic) `source` paths must not mojibake; `version`
   gating (absent→1, `!=1`→error); duplicate clip ids, out-of-range
   `track`, negative times, unknown transition-type strings; transitions
   referencing missing/non-adjacent clips must self-heal via
   `DropInvalidTransitions`, not corrupt the load.
2. **Undo/redo correctness**: macro grouping (drag/trim gesture =
   `BeginMacro`…`EndMacro` → ONE undo step), `MergeWith` coalescing of
   consecutive move/trim edits, transition drop-on-edit + restore-on-
   undo, `SplitClip` id remap stability across undo→redo (new id must
   be generated once, not per-Apply), undo restoring clip vector
   positions and the FULL transition set (an `AddClip` between two
   transitioned clips drops that transition — undo must restore it).
3. **SQLite schema/migration + path dedup**: fresh-DB `CREATE TABLE
   IF NOT EXISTS` + `CREATE UNIQUE INDEX`; reopen is a no-op; upsert
   keeps `added_at` but refreshes probe fields; `path` dedup is
   case-insensitive on Windows (`D:/a.MP4` vs `d:/a.mp4`) and
   canonical (`.`/`..` resolved); unicode paths bind as UTF-8;
   `Remove` deletes the row and best-effort the poster file.
4. **Poster extraction edge cases**: audio-only file → clean `false`
   (no crash, no file); missing/unreadable file; `seek_seconds` beyond
   EOF clamps to last frame; output is a real PNG (magic bytes); parent
   dirs of `out_png_path` are created; non-Windows builds fail softly
   (`VideoDecoder::Open` stub → false).
5. **Catalog path resolution on non-EN Windows**: `LOCALAPPDATA`
   containing non-ASCII (e.g. `C:\Users\Дмитрий\...`) arrives as ANSI
   bytes via `_dupenv_s` — must pass through `NormalizeUtf8Path` before
   `FromUtf8`; unset env → fallback chain (`XDG_DATA_HOME` →
   `$HOME/.local/share` → temp dir); DB + poster paths stored/derived
   as canonical UTF-8 so SQLite `=`/`COLLATE NOCASE` dedup works.

---

## Task 1: `XER_VideoEditor_Core` lib + `timeline_model` + test harness

**Files:**
- Create: `tools/video_editor/CMakeLists.txt`
- Create: `tools/video_editor/timeline_model.h`,
  `tools/video_editor/timeline_model.cc`
- Create: `tests/video/video_editor_test_util.h`,
  `tests/video/video_editor_test_main.cc`,
  `tests/video/video_editor_model_tests.cc`
- Modify: `CMakeLists.txt` (root — `add_subdirectory(tools/video_editor)`
  before `include(CTest)` ~line 461)
- Modify: `tests/CMakeLists.txt` (append `XER_VideoEditor_Tests`)

**Interfaces:**
- Produces (exact signatures — `tools/video_editor/timeline_model.h`,
  namespace `XER::VideoEditor`):

```cpp
enum class TrackKind { Video, Audio };

struct Transform {
    float offset_x = 0.0f;      // clip top-left, fraction of canvas w/h
    float offset_y = 0.0f;
    float scale = 1.0f;         // uniform multiplier on source pixels
    float rotation_deg = 0.0f;
    float opacity = 1.0f;
};
struct ClipEffect { std::string type; std::vector<float> params; };
struct Clip {
    std::string id;             // uuid (NewClipId)
    std::string source_path;    // UTF-8
    int track = 0;              // index into Project::tracks
    double timeline_start = 0;  // seconds
    double in_point = 0;        // source seconds
    double duration = 0;        // timeline seconds; out = in + duration
    bool muted = false;
    float gain_db = 0;
    double fade_in = 0, fade_out = 0;   // seconds
    Transform transform;
    std::vector<ClipEffect> effects;
};
struct Transition {
    std::string from_clip, to_clip;     // adjacent on same VIDEO track
    enum class Type {
        Crossfade, WipeLeft, WipeRight, WipeUp, WipeDown,
        SlideLeft, SlideRight, SlideUp, SlideDown
    } type = Type::Crossfade;
    double duration = 1.0;              // seconds, centered on the cut
};
struct Track {
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool muted = false;    // audio: silence; video: ignored (use hidden)
    bool hidden = false;   // video: not composited (+ its audio); ignored on audio
    float gain_db = 0;     // stored for Phase 2
};
struct Project {
    int width = 1920, height = 1080;
    int fps_num = 30, fps_den = 1;
    std::array<std::uint8_t,4> bg_color{0,0,0,255};
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::vector<Transition> transitions;
    double duration() const;   // max(timeline_start + duration), 0 if empty
};

// Source bounds for invariant checks. duration_seconds <= 0 means
// "unknown" → no upper clamp; has_video == false restricts the clip to
// audio tracks.
struct MediaBounds {
    double duration_seconds = 0.0;
    bool has_video = true;
};

enum class TrimEdge { In, Out };

// ---- queries ----
const Clip*  FindClip(const Project&, const std::string& clip_id);
Clip*        FindClip(Project&, const std::string& clip_id);
const Track* FindTrack(const Project&, int track_index);
double       ClipEndSeconds(const Clip&);                    // start + duration
bool         ClipActiveAt(const Clip&, double t);            // start <= t < end
std::vector<const Clip*> ClipsOnTrack(const Project&, int track_index);  // by start
std::vector<Clip*>       ClipsOnTrack(Project&, int track_index);
// Sorted unique snap candidates: 0, every clip start/end on
// `track_index` (all tracks when < 0), plus `playhead` when >= 0.
std::vector<double> CollectSnapPoints(const Project&, int track_index,
                                      double playhead);
// `candidate` snapped to the nearest snap point within `threshold`
// seconds (ties → earlier point), else `candidate` unchanged.
double SnapTime(const Project&, int track_index, double candidate,
                double threshold, double playhead);

// ---- mutations (validate-then-mutate; transactional; error_out) ----
// Shared placement validation: track index valid; has_video==false
// requires TrackKind::Audio; id non-empty/unique (unless it equals
// exclude_clip_id); duration > kMinClipDuration; start >= 0;
// in_point >= 0; in_point + duration <= media duration when known;
// no overlap on the track excluding exclude_clip_id.
bool ValidateClipPlacement(const Project&, const Clip&, const MediaBounds&,
                           const std::string& exclude_clip_id,
                           std::string* error_out);

int  AddTrack(Project&, TrackKind kind, const std::string& name);  // index or -1
// Only when the track holds no clips; reindexes clip.track > index.
bool RemoveTrack(Project&, int track_index, std::string* error_out);

bool AddClip(Project&, const Clip&, const MediaBounds&, std::string* error_out);
bool RemoveClip(Project&, const std::string& clip_id);
bool MoveClip(Project&, const std::string& clip_id, double new_timeline_start,
              const MediaBounds&, std::string* error_out);
// TrimEdge::In  — new_point is a new in_point (source s);
//   timeline_start += (new_in - old_in); duration = old_out - new_in.
// TrimEdge::Out — new_point is a new out_point (source s);
//   duration = new_out - in_point; timeline_start unchanged.
bool TrimClip(Project&, const std::string& clip_id, TrimEdge edge,
              double new_point, const MediaBounds&, std::string* error_out);
// split_timeline_t strictly inside (start + kMinClipDuration,
// end - kMinClipDuration). First half keeps `clip_id`; second half gets
// `new_clip_id` (caller-supplied, non-empty, unique). Second half:
// in_point += split_t - start; timeline_start = split_t;
// duration = end - split_t. Both keep muted/gain/transform/effects;
// fade_in stays on first half (second gets 0), fade_out stays on second
// (first gets 0). Transitions with to_clip==id keep pointing at the
// first half; transitions with from_clip==id are re-pointed to
// new_clip_id.
bool SplitClip(Project&, const std::string& clip_id, double split_timeline_t,
               const std::string& new_clip_id, std::string* error_out);
// Inspector/track-move edits: replacement.id is ignored — the stored
// clip keeps `clip_id`; all other fields (incl. track) come from
// `replacement` and are re-validated.
bool ReplaceClip(Project&, const std::string& clip_id, const Clip& replacement,
                 const MediaBounds&, std::string* error_out);
bool AddTransition(Project&, const std::string& from_clip,
                   const std::string& to_clip, Transition::Type type,
                   double duration, std::string* error_out);
bool RemoveTransition(Project&, const std::string& from_clip,
                      const std::string& to_clip);
// Removes every transition that is not video-track, missing an
// endpoint, on different tracks, or non-adjacent (to.start ≈
// from.end). Called by every mutating op and by project_store load.
void DropInvalidTransitions(Project&);

// Centered letterbox fit: scale = min(canvas_w/src_w,
// canvas_h/src_h); offset = (canvas - src*scale)/2 as a fraction of
// canvas w/h. Returns identity Transform{} when any dim <= 0.
Transform FitTransformToCanvas(int src_w, int src_h, int canvas_w,
                               int canvas_h);
std::string NewClipId();   // uuid v4, "8-4-4-4-12" hex
```

- Consumes: nothing outside std (pure module). Test exe links
  `XER_VideoEditor_Core`.
- Produces: `XER_VideoEditor_Core` static lib + `XER_VideoEditor_Tests`
  ctest exe — the skeleton every later task extends.

- [ ] **Step 1: Write the failing test harness + model tests**

Create `tests/video/video_editor_test_util.h` (shared plain-assert
harness — header-only so every suite .cc shares one failure counter):

```cpp
// Shared plain-assert harness for XER_VideoEditor_Tests — same style
// as tests/video/xer_video_smoke.cc (process exit code = failure count)
// but header-only/inline so multiple suite .cc files share one counter.
#pragma once

#include <cstdio>
#include <filesystem>

namespace XER::VideoEditor::Test {

inline int g_failures = 0;

inline void Check(bool condition, const char* expression,
                  const char* file, int line) {
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

inline int Failures() { return g_failures; }

inline std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_video_editor_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace XER::VideoEditor::Test

#define XER_VE_CHECK(cond) \
    XER::VideoEditor::Test::Check(static_cast<bool>(cond), #cond, \
                                  __FILE__, __LINE__)
```

Create `tests/video/video_editor_test_main.cc`:

```cpp
// Single entry point for the VideoEditor headless-core suites. Each
// suite .cc exposes XER_VideoEditor_Run<Suite>Tests(); the shared
// XER_VE_CHECK macro counts failures in
// XER::VideoEditor::Test::g_failures (video_editor_test_util.h).
#include <cstdio>

#include "video_editor_test_util.h"

int XER_VideoEditor_RunModelTests();
int XER_VideoEditor_RunHistoryTests();
int XER_VideoEditor_RunStoreTests();
int XER_VideoEditor_RunCatalogTests();
int XER_VideoEditor_RunPosterTests();

int main() {
    XER_VideoEditor_RunModelTests();
    XER_VideoEditor_RunHistoryTests();
    XER_VideoEditor_RunStoreTests();
    XER_VideoEditor_RunCatalogTests();
    XER_VideoEditor_RunPosterTests();
    const int failures = XER::VideoEditor::Test::Failures();
    if (failures == 0) {
        std::printf("xer_video_editor_tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "xer_video_editor_tests: %d check(s) failed\n",
                 failures);
    return failures > 255 ? 255 : failures;
}
```

Create `tests/video/video_editor_model_tests.cc` — the model is pure,
so this file is the TDD contract for Task 1. Required cases (write as
`int XER_VideoEditor_RunModelTests()` using `XER_VE_CHECK`):

```cpp
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
}

- [ ] **Step 2: Create the lib + test registration (build → link error = RED)**

Create `tools/video_editor/CMakeLists.txt`:

```cmake
# VideoEditor core — headless timeline model/history/store/catalog/
# poster modules (Phase 1B). UI-free; consumed by the future
# video_editor exe (1C/1D) and XER_VideoEditor_Tests.
# CONFIGURE_DEPENDS so adding .cc files does not require a manual
# re-configure under Ninja.
file(GLOB CONFIGURE_DEPENDS VIDEO_EDITOR_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

add_library(XER_VideoEditor_Core STATIC ${VIDEO_EDITOR_CORE_SOURCES})

target_include_directories(XER_VideoEditor_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(XER_VideoEditor_Core
    PUBLIC
        XER_Video          # video::Frame / FrameExporter (clip_poster)
        XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
        XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
        XER_SQL            # SQLiteDatabase (media_catalog)
        XER_Helper         # Path/env/jsoncpp helpers
        jsoncpp_lib        # project_store (.xerproj)
)

target_compile_features(XER_VideoEditor_Core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_VideoEditor_Core PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_VideoEditor_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
```

Root `CMakeLists.txt`: insert before `include(CTest)` (~line 461):

```cmake
# VideoEditor (XER Studio Phase 1) — headless core lib; the exe lands in
# 1C/1D inside this same directory.
add_subdirectory(tools/video_editor)
```

`tests/CMakeLists.txt`: append:

```cmake
# VideoEditor headless core — model/history/store/catalog/poster unit
# tests. Plain-assert style like XER_Video_Smoke; FFmpeg-backed checks
# self-skip via XER::Bridge::FFmpeg::IsAvailable(). Registered
# unconditionally: XER_VideoEditor_Core's FFmpeg deps degrade to stubs
# off-Windows and unused XER_Video objects are never pulled from the
# static lib (if a non-MSVC link ever breaks, gate with if(MSVC) like
# XER_Video_Smoke).
add_executable(XER_VideoEditor_Tests
    video/video_editor_test_util.h
    video/video_editor_test_main.cc
    video/video_editor_model_tests.cc
    video/video_editor_history_tests.cc
    video/video_editor_store_tests.cc
    video/video_editor_catalog_tests.cc
    video/video_editor_poster_tests.cc
)
target_link_libraries(XER_VideoEditor_Tests PRIVATE XER_VideoEditor_Core)
target_compile_features(XER_VideoEditor_Tests PRIVATE cxx_std_23)
add_test(NAME XER_VideoEditor_Tests COMMAND XER_VideoEditor_Tests)
```

(History/store/catalog/poster suite files don't exist yet — either
create them as stubs returning 0 in this step, or add their
`add_executable` sources line-by-line as each task lands. Preferred:
create all five suite files now as `int
XER_VideoEditor_Run<Suite>Tests() { return 0; }` stubs so the exe links
once and later tasks only fill bodies — keeps each task's diff focused
on its own module. The stub files are listed in the task that creates
them? NO — create all stubs HERE so tests/CMakeLists.txt is written
once; each later task replaces its stub body. State this in the commit.)

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --preset default` then
`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
Expected: **compile/link errors** — `timeline_model.h` doesn't exist
yet (RED).

- [ ] **Step 3: Implement `timeline_model.{h,cc}`**

Write `tools/video_editor/timeline_model.h` with the exact declarations
above. `FitTransformToCanvas` implements the centered-letterbox formula
documented in its header comment (`scale = min(canvas_w/src_w,
canvas_h/src_h)`, offsets = `(canvas - src*scale)/2` as canvas
fractions; identity `Transform{}` when any dimension ≤ 0).

`timeline_model.cc` implementation rules:

- `Project::duration()`: `max(clip.timeline_start + clip.duration)`.
- `kMinClipDuration = 0.001` (1 ms floor — builder call; spec sets no
  minimum, this only rejects degenerate clips), `kEpsilon = 1e-6`
  (adjacency/snap tolerance, seconds).
- `ValidateClipPlacement`: track index in range; `bounds.has_video ==
  false` ⇒ `tracks[track].kind == TrackKind::Audio`; `clip.id` empty or
  colliding with a DIFFERENT existing clip → error; `duration >
  kMinClipDuration`; `timeline_start >= 0`; `in_point >= 0`; when
  `bounds.duration_seconds > 0` require `in_point + duration <=
  bounds.duration_seconds + kEpsilon`; overlap check — for every other
  clip on the same track (excluding `exclude_clip_id`): reject when
  `c.timeline_start < new_end && new_start < ClipEndSeconds(c)`.
- `AddClip`: validate → `clips.push_back` → `DropInvalidTransitions`.
- `RemoveClip`: erase clip by id → `DropInvalidTransitions` (transitions
  referencing it drop; unrelated ones survive — adjacency of survivors
  is unchanged).
- `MoveClip`: build modified clip, `ValidateClipPlacement(exclude=id)`,
  assign fields in place (preserves vector position) →
  `DropInvalidTransitions`.
- `TrimClip`: compute new `in_point`/`duration`/`timeline_start` per the
  documented edge semantics; reject when `new_duration <=
  kMinClipDuration` or `new_in_point < 0` or (bounds known) `new_out >
  bounds.duration_seconds`; build modified clip →
  `ValidateClipPlacement(exclude=id)` → assign → drop invalid.
- `SplitClip`: `FindClip`; split strictly inside
  `(start + kMin, end - kMin)`; `new_clip_id` non-empty and unique;
  first half `duration = split_t - start`; second half `in_point +=
  split_t - start`, `timeline_start = split_t`, `duration = end -
  split_t`, fades split per header doc; `clips.push_back(second)` →
  rewrite transitions: `from_clip == id` → `new_clip_id`
  (`to_clip == id` stays — first half keeps the id and the cut
  position) → `DropInvalidTransitions`.
- `ReplaceClip`: `replacement.id = clip_id` (ignored input), keep the
  clip's vector slot: validate → assign all fields → drop invalid.
- `AddTransition`: both clips exist, `from != to`, same `track`,
  `tracks[track].kind == Video`, adjacent (`to.timeline_start ≈
  ClipEndSeconds(from)`), `duration > 0`, no existing transition on the
  same `(from,to)` pair → push_back.
- `RemoveTransition`: erase matching `(from,to)` pair.
- `DropInvalidTransitions`: `erase_if` where EITHER endpoint clip
  missing, endpoints on different tracks, track kind != Video, or NOT
  adjacent — adjacency = same track AND
  `fabs(to.timeline_start - ClipEndSeconds(from)) <= kEpsilon`
  (sufficient under the no-overlap invariant: nothing can sit between
  two clips whose boundaries touch).
- `AddTrack`: `tracks.push_back({kind, name})` → return index.
- `RemoveTrack`: reject out-of-range or any clip with `track ==
  index`; `tracks.erase(begin+index)`; then `for (auto& c : clips)
  if (c.track > index) --c.track;`.
- `CollectSnapPoints`: `{0} ∪ {clip start,end on track (all tracks if
  index < 0)} ∪ {playhead if >= 0}` → sort, unique with kEpsilon.
- `SnapTime`: nearest point within `threshold`, else `candidate`.
- `NewClipId`: uuid v4 from `std::random_device` — two 64-bit draws,
  set version nibble `4` and variant bits `10`, format
  `%08x-%04x-%04x-%04x-%012llx`-style hex with dashes.

- [ ] **Step 4: Build + run — verify model tests PASS**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
Then: `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure -R XER_VideoEditor`
(or run `out/build/default/bin/XER_VideoEditor_Tests.exe` directly —
it prints per-check failures and exits nonzero on any).
Expected: all model checks pass; history/store/catalog/poster stubs
return 0.

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/ tests/video/ CMakeLists.txt tests/CMakeLists.txt
git commit -m "Add VideoEditor timeline model, core lib, and test harness"
```

---

## Task 2: `timeline_history` (undo/redo)

**Files:**
- Create: `tools/video_editor/timeline_history.h`,
  `tools/video_editor/timeline_history.cc`
- Modify: `tests/video/video_editor_history_tests.cc` (replace stub)

**Interfaces:**
- Consumes: all of `timeline_model` (Task 1).
- Produces (`tools/video_editor/timeline_history.h`, namespace
  `XER::VideoEditor`):

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual bool Apply(Project& project) = 0;
    virtual bool Revert(Project& project) = 0;
    virtual std::string Label() const = 0;
    virtual bool MergeWith(const Command&) { return false; }
};

class TimelineHistory {
public:
    explicit TimelineHistory(Project& project, std::size_t max_depth = 50);
    ~TimelineHistory();
    TimelineHistory(const&) = delete;
    TimelineHistory& operator=(const&) = delete;

    bool Execute(std::unique_ptr<Command> command);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    void Clear();

    void BeginMacro(const std::string& label);
    void EndMacro();

    // Convenience wrappers — EVERY model mutation goes through these.
    bool AddClip(Clip clip, const MediaBounds& bounds,
                 std::string* clip_id_out = nullptr,
                 std::string* error_out = nullptr);
    bool RemoveClip(const std::string& clip_id);
    bool MoveClip(const std::string& clip_id, double new_timeline_start,
                  const MediaBounds& bounds, std::string* error_out = nullptr);
    bool TrimClip(const std::string& clip_id, TrimEdge edge,
                  double new_point, const MediaBounds& bounds,
                  std::string* error_out = nullptr);
    bool SplitClip(const std::string& clip_id, double split_timeline_t,
                   std::string* new_clip_id_out = nullptr,
                   std::string* error_out = nullptr);
    bool ReplaceClip(const std::string& clip_id, const Clip& replacement,
                     const MediaBounds& bounds, std::string* error_out = nullptr);
    bool AddTransition(const std::string& from_clip,
                       const std::string& to_clip, Transition::Type type,
                       double duration, std::string* error_out = nullptr);
    bool RemoveTransition(const std::string& from_clip,
                          const std::string& to_clip);
    int  AddTrack(TrackKind kind, const std::string& name);
    bool RemoveTrack(int track_index, std::string* error_out = nullptr);

private:
    class MacroCommand;
    Project& project_;
    std::size_t max_depth_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::vector<std::unique_ptr<MacroCommand>> macro_stack_;
};
```

- Consumed by: 1C/1D (every widget/inspector edit), tests now.

- [ ] **Step 1: Write the failing history tests**

Replace the stub `tests/video/video_editor_history_tests.cc` with
`int XER_VideoEditor_RunHistoryTests()` covering (all through
`TimelineHistory`, comparing full `Project` state where practical —
write a small `ProjectsEqual` helper comparing tracks/clips/
transitions field-by-field with 1e-9 tolerance on doubles):

```cpp
#include "video_editor_test_util.h"
#include "timeline_history.h"

// Cases (each a block in RunHistoryTests):
// 1. AddClip via history → CanUndo, Undo removes it, Redo restores,
//    UndoLabel() == "Add clip".
// 2. MoveClip undo restores position AND a transition that the move
//    dropped (set up x|a|y adjacent + T(x,a); move "a" to overlap-free
//    gap → transition drops; Undo → clip back AND transition back).
// 3. TrimClip undo restores in/out/duration/start.
// 4. SplitClip: history.SplitClip returns new id via out-param; undo →
//    single clip "a" with original fields and BOTH original transitions
//    (x->a, a->y) intact; redo → two clips again with SAME new id
//    (determinism across redo — capture the id from first apply and
//    assert redo produces the identical project).
// 5. Macro: BeginMacro("drag") ; MoveClip a→b→c positions ; EndMacro →
//    ONE Undo restores pre-macro state; CanRedo after undo; nested
//    BeginMacro inside macro merges into one step.
// 6. MergeWith: two consecutive MoveClip on same clip coalesce — after
//    Execute(m1);Execute(m2): undo_.size()==1 effectively (verify via
//    single Undo returning to ORIGINAL position, not intermediate).
//    A MoveClip on a DIFFERENT clip does NOT merge (two undo steps).
// 7. ReplaceClip (inspector edit: gain_db) undo/redo.
// 8. AddTransition/RemoveTransition undo/redo; AddTransition on bad
//    cut returns false and does NOT push history (CanUndo unchanged).
// 9. Depth cap: set max_depth=5 in a local TimelineHistory, do 8 edits,
//    undo 5 times reaches the 3rd edit's before-state (oldest dropped).
// 10. Clear() empties both stacks; Execute failure (e.g. MoveClip onto
//     overlap) leaves history unchanged.
// 11. RemoveTrack through history reindexes clip.track on undo/redo.
```

- [ ] **Step 2: Build — verify link error (RED)**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
Expected: undefined `XER_VideoEditor_RunHistoryTests` internals →
actually the stub returns 0 so it LINKS; the RED here is behavioral —
the new tests can't compile against missing `timeline_history.h`.
Write the test file FIRST (it `#include "timeline_history.h"`) so the
build fails at compile time — that IS the red. Then implement.

- [ ] **Step 3: Implement `timeline_history.{h,cc}`**

`timeline_history.cc` structure (mirror `node_graph_history.cc`):

```cpp
namespace {

// What a clip-affecting command must restore on Revert. before_clips
// holds (vector_index, Clip) for exactly the ids the command touches;
// before_transitions is a FULL copy of project.transitions (small —
// transitions are few; full copy makes revert trivially correct and
// future-proof — today only transitions referencing the touched ids
// can break, but the full restore costs nothing). after_clip_ids
// lists clips to erase before reinserting before_clips (split's
// second half, added clips).
struct TimelineSlice {
    std::optional<std::vector<Track>> before_tracks;  // track commands only
    std::vector<std::pair<std::size_t, Clip>> before_clips;
    std::vector<Transition> before_transitions;
    std::vector<std::string> after_clip_ids;
};

TimelineSlice CaptureSlice(const Project& p,
                           const std::vector<std::string>& clip_ids,
                           bool include_tracks) {
    TimelineSlice s;
    if (include_tracks) s.before_tracks = p.tracks;
    s.before_transitions = p.transitions;
    for (const auto& id : clip_ids) {
        for (std::size_t i = 0; i < p.clips.size(); ++i) {
            if (p.clips[i].id == id) {
                s.before_clips.emplace_back(i, p.clips[i]);
                break;
            }
        }
    }
    return s;
}

void RestoreSlice(Project& p, const TimelineSlice& s) {
    if (s.before_tracks) p.tracks = *s.before_tracks;
    // Erase after-state clips, then restore before-state clips at their
    // recorded indices (clamped — later erases may shift positions).
    if (!s.after_clip_ids.empty()) {
        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(),
            [&](const Clip& c) {
                return std::find(s.after_clip_ids.begin(),
                                 s.after_clip_ids.end(), c.id) !=
                       s.after_clip_ids.end();
            }), p.clips.end());
    }
    p.transitions = s.before_transitions;   // full restore
    auto restored = s.before_clips;         // sort ascending by index
    std::sort(restored.begin(), restored.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, clip] : restored) {
        const std::size_t at = std::min(index, p.clips.size());
        p.clips.insert(p.clips.begin() + static_cast<std::ptrdiff_t>(at), clip);
    }
}
```

Ordering rule for `RestoreSlice` (verified): erase `after_clip_ids`
clips first, restore `before_transitions` wholesale, then re-insert
`before_clips` in ASCENDING recorded-index order at `min(index,
clips.size())`. Ascending is required — each insert restores the
prefix `[0, index)` so later targets stay correct (descending puts
later clips past their slots). `before_clips` is captured in ascending
order naturally; sort a copy defensively anyway.

Command classes (all in anonymous namespace):

- `ClipEditCommand : public Command` — base holding `TimelineSlice
  slice_; bool captured_ = false;` + `virtual bool ApplyOp(Project&,
  std::string* error) = 0;` + `virtual void NoteAfter(std::vector<
  std::string> ids) = 0;` (sets `slice_.after_clip_ids`).
  `Apply(project)`: `if (!captured_) { slice_ = CaptureSlice(project,
  affected_ids_, include_tracks_); captured_ = true; }` →
  `ApplyOp` → on success `NoteAfter(...)`. `Revert(project)`:
  `RestoreSlice(project, slice_); return true;`
  `affected_ids_`/`include_tracks_` are ctor params.
- Concrete commands store their params + call the matching model op in
  `ApplyOp` (which is transactional — model validates before mutating,
  so a failed ApplyOp leaves the project untouched and Execute drops
  the command):
  - `AddClipCommand(Clip, MediaBounds)` — clip.id pre-filled by the
    wrapper (NewClipId when empty); affected={id}; after={id}; Label
    "Add clip".
  - `RemoveClipCommand(id)` — affected={id}; after={} ; Label "Remove
    clip".
  - `MoveClipCommand(id, new_start, MediaBounds)` — affected={id};
    after={id}; MergeWith: same dynamic type + same id → `target_ =
    other.target_`.
  - `TrimClipCommand(id, edge, point, MediaBounds)` — same; MergeWith
    requires same id AND same edge.
  - `SplitClipCommand(id, split_t, new_id)` — new_id generated ONCE in
    the wrapper (`NewClipId()`) so redo is deterministic; affected={id};
    after={id, new_id}; Label "Split clip".
  - `ReplaceClipCommand(id, replacement, MediaBounds)` — affected={id};
    after={id}; MergeWith same id → `replacement_ = other.replacement_`.
  - `AddTrackCommand(kind,name)` — affected={}, include_tracks=true;
    after={}; ApplyOp=`AddTrack` (returns index, discarded).
  - `RemoveTrackCommand(index)` — affected = ids of clips with
    `track > index` (their .track changes), include_tracks=true;
    ApplyOp=`RemoveTrack`; after = same affected ids (they persist,
    modified) so Revert erases+restores them.
- Dedicated non-slice commands:
  - `AddTransitionCommand(from,to,type,duration)` — stores params;
    Apply: `AddTransition(project_,...)`; Revert: `RemoveTransition`;
    Label "Add transition".
  - `RemoveTransitionCommand(from,to)` — Apply captures the
    `Transition` (must exist) then `RemoveTransition`; Revert pushes it
    back (validate adjacency not needed — LIFO guarantees prior state);
    Label "Remove transition".
- `TimelineHistory::MacroCommand` — nested class like NodeGraph's:
  `Apply` forwards to children in order, `Revert` in REVERSE order,
  `Label` = macro label, `Add`/`empty`.

`TimelineHistory` method bodies mirror `node_graph_history.cc:246-299`
exactly (Execute applies→clears redo→macro-or-merge-or-push with
depth cap; Undo/Redo guards on `!macro_stack_.empty()`; EndMacro nests
into parent macro or pushes to undo_). Wrappers construct commands and
call `Execute`; `AddClip`/`SplitClip` fill empty ids via `NewClipId()`
before constructing (deterministic across redo) and surface them via
the out-params on success. `AddTrack` uses the NodeGraph `AddNode`
pattern — keep `auto* raw = cmd.get()` before `Execute(std::move(cmd))`
(the pointee outlives the move) and return `raw->index_` on success,
`-1` on failure.

- [ ] **Step 4: Build + run — verify history tests PASS**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
Then the exe / `ctest -R XER_VideoEditor`. Expected: all history checks
pass; rerun the model suite (regression).

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/timeline_history.{h,cc} tests/video/video_editor_history_tests.cc
git commit -m "Add TimelineHistory undo/redo for the video editor model"
```

---

## Task 3: `project_store` (`.xerproj` JSON v1)

**Files:**
- Create: `tools/video_editor/project_store.h`,
  `tools/video_editor/project_store.cc`
- Modify: `tests/video/video_editor_store_tests.cc` (replace stub)

**Interfaces:**
- Consumes: `timeline_model` (Task 1), `jsoncpp_lib` (`<json/json.h>`).
- Produces:

```cpp
class ProjectStore {
public:
    static constexpr int kVersion = 1;
    // UTF-8 paths. Save writes <path>.tmp then renames (crash-safe);
    // on Windows rename-over-existing may need remove+retry — handle
    // with std::error_code, never throw.
    static bool Save(const Project&, const std::string& utf8_path,
                     std::string* error_out = nullptr);
    static bool Load(const std::string& utf8_path, Project* project_out,
                     std::string* error_out = nullptr);
};
```

JSON shape = spec example verbatim (keys: `version`, `canvas{width,
height,fps_num,fps_den,bg_color[4]}`, `tracks[{kind,name,muted,hidden,
gain_db}]`, `clips[{id,source,track,timeline_start,in_point,duration,
muted,gain_db,fade_in,fade_out,transform{offset_x,offset_y,scale,
rotation_deg,opacity},effects[{type,params[]}]}]`,
`transitions[{from,to,type,duration}]`).

- [ ] **Step 1: Write the failing store tests**

Replace stub with `int XER_VideoEditor_RunStoreTests()`:

```cpp
// Cases:
// 1. Round-trip: project with 2 tracks (video+audio), 3 clips covering
//    EVERY Clip field (non-default transform, 2 effects with param
//    arrays, fades, gain, muted), 1 transition → Save to
//    TempDir()/roundtrip.xerproj → Load → field-by-field equality
//    (doubles exact or 1e-9; floats compare as float).
// 2. UTF-8 path: clip source "D:/медиа/видео 🎬.mp4" survives
//    byte-identical (compare std::string, not locale).
// 3. Version gate: hand-edit saved JSON "version":2 → Load false +
//    error mentions version; "version" absent → loads as v1.
// 4. Robustness: missing file → false; garbage bytes → false; root
//    not an object → false.
// 5. Leniency: delete optional fields (track.name, clip.effects,
//    transform) from JSON → loads with struct defaults; clip missing
//    "id" → error; duplicate clip ids → error; clip.track out of range
//    → error; negative duration → error.
// 6. Self-heal: JSON whose transition references a missing clip id or
//    non-adjacent clips → Load succeeds, transitions empty
//    (DropInvalidTransitions).
// 7. Empty project round-trips (zero tracks/clips/transitions).
```

- [ ] **Step 2: Build — verify compile error (RED)**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests`
→ `project_store.h` not found.

- [ ] **Step 3: Implement `project_store.{h,cc}`**

`project_store.cc` notes:

- Read file via `std::ifstream` + `Json::CharReaderBuilder` (or
  `XER::Helper::JsonCppHelper::ParseFile` — check its behavior first;
  prefer raw jsoncpp `parseFromStream` for explicit error strings).
- `Save`: build `Json::Value root(Json::objectValue)`; write with
  `Json::StreamWriterBuilder` (no indent — compact; `settings_[
  "indentation"]=""`); emit to `<path>.tmp` via `std::ofstream` in
  binary mode, flush+check, then `std::filesystem::rename` — on
  failure `std::error_code` → `fs::remove(target)` + retry once.
  Paths: wrap with `XER::Helper::Path::FromUtf8` for ofstream? —
  `std::ofstream` takes `const char*`/`fs::path`; on Windows use
  `fs::path` from `FromUtf8` so non-ASCII `.xerproj` paths work.
- `Load`: parse → validate → fill `Project` → `DropInvalidTransitions`
  → return. Field readers: small `GetDouble/GetInt64/GetBool/
  GetString` helpers tolerating absent keys (jsoncpp `isMember` checks)
  but ERRORING on present-wrong-type (`isDouble/isInt64/isBool/isString`
  guards — jsoncpp silently converts otherwise).
- Enum mappings (anonymous namespace):
  `TrackKindToString`/`TrackKindFromString` ("video"/"audio", unknown→
  error); `TransitionTypeToString`/`FromString` for the 9 spec types
  ("crossfade","wipe_left","wipe_right","wipe_up","wipe_down",
  "slide_left","slide_right","slide_up","slide_down"; unknown→error).
- Validation on load (strict for primary data, self-heal for derived):
  `version` absent→1, `!=1`→error; canvas ints must be positive when
  present; `bg_color` array of exactly 4 ints 0-255 when present;
  clip: `id` required non-empty + unique, `source` required non-empty,
  `track` int in `[0, tracks.size())`, `duration > 0`, `timeline_start
  >= 0`, `in_point >= 0`; `effects[].params` array of numbers;
  `transform` object fields default to `Transform{}` when absent;
  transitions parsed leniently (missing endpoints drop via
  DropInvalidTransitions; unknown `type` string → error).
- `duration`/`timeline_start`/etc. write as JSON numbers (doubles) —
  jsoncpp round-trips doubles exactly; `gain_db`/`scale` floats write
  as double, read back `static_cast<float>` — exact for float-origin
  values.

- [ ] **Step 4: Build + run — verify store tests PASS**

Same build+run commands. Expected: round-trip byte-stable semantics,
all gate cases pass.

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/project_store.{h,cc} tests/video/video_editor_store_tests.cc
git commit -m "Add .xerproj JSON v1 project store for VideoEditor"
```

---

## Task 4: `clip_poster` (poster frame extraction)

**Files:**
- Create: `tools/video_editor/clip_poster.h`,
  `tools/video_editor/clip_poster.cc`
- Modify: `tests/video/video_editor_poster_tests.cc` (replace stub)

**Interfaces:**
- Consumes: `XER::Bridge::FFmpeg::VideoDecoder`
  (`wrapper/ffmpeg/ffmpeg_video_decoder.h`), `video::Frame` +
  `video::FrameExporter` (`frame.h`, `frame_exporter.h` via `XER_Video`).
- Produces:

```cpp
namespace XER::VideoEditor {

// Writes a PNG poster for a media file. `seek_seconds` is clamped to
// [0, duration]; the decoder seeks keyframe-backward then decodes
// forward (bounded) to the first frame at/after the target — falls
// back to the last decoded frame at EOF. Returns false with
// error_out set for: audio-only files (GetWidth/Height == 0), decode
// failure, or export failure. UTF-8 paths; parent dirs of
// utf8_out_png_path are created. Stateless/static — callers own
// concurrency.
class ClipPoster {
public:
    static bool Extract(const std::string& utf8_source_path,
                        double seek_seconds,
                        const std::string& utf8_out_png_path,
                        std::string* error_out = nullptr);
};

}  // namespace XER::VideoEditor
```

Spec note: spec says poster `<id>.jpg` but `FrameExporter::ToFile` is
PNG-only (`codec_name="png"`, frame_exporter.cc:45) and `ImageToJpeg`
only decodes the FIRST frame — PNG via decoder-seek is the spec'd
pipeline ("VideoDecoder seek to mid-clip → NextFrame →
FrameExporter::ToFile (PNG)"). Catalog stores the real `.png` path;
the `.jpg` in the spec schema comment is illustrative.

- [ ] **Step 1: Write the failing poster tests**

Replace stub with `int XER_VideoEditor_RunPosterTests()`:

```cpp
// Whole suite gated on XER::Bridge::FFmpeg::IsAvailable() (return early
// otherwise — same as XER_Video_Smoke / ffmpeg_*_tests).
// Fixture helpers (copy the sine/provoder patterns from
// tests/video/xer_video_smoke.cc and tests/ui/ffmpeg_av_mux_tests.cc):
//   - MakeVideoFile(path): EncodeVideoFrameStream, 2 s of 64x48 BGRA
//     moving-bar frames @15fps, codec fallback {"libx264","mpeg4"}.
//   - MakeAudioFile(path): EncodeAudioFrames pcm_s16le 1 s sine wav.
// Cases:
// 1. Extract(video, duration*0.5, out.png) → true; file exists; first
//    4 bytes == 89 50 4E 47 (PNG magic); size > 100 bytes.
// 2. Extract(audio_only.wav, 0.5, out.png) → FALSE (audio-only has no
//    pictures) and no file created.
// 3. Extract(missing file) → false, error non-empty.
// 4. Seek clamp: Extract(video, 999.0, out2.png) → true (clamped to
//    last frame); Extract(video, -5.0, out3.png) → true (clamped to 0).
// 5. Nested out path (tempdir/sub/dir/poster.png) → parent dirs created.
```

- [ ] **Step 2: Build — verify compile error (RED)**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests` → `clip_poster.h` not found.

- [ ] **Step 3: Implement `clip_poster.{h,cc}`**

```cpp
bool ClipPoster::Extract(const std::string& utf8_source_path,
                         double seek_seconds,
                         const std::string& utf8_out_png_path,
                         std::string* error_out) {
    // 1. create_directories(parent of FromUtf8(out path), ec).
    // 2. XER::Bridge::FFmpeg::VideoDecoder decoder;
    //    decoder.Open(utf8_source_path, &err) — false → error_out, false.
    //    (RAII: decoder.Close via destructor; no manual cleanup needed —
    //     the class manages impl_.)
    // 3. if (decoder.GetWidth() <= 0 || decoder.GetHeight() <= 0)
    //       → error "no video stream (audio-only)"; false.
    // 4. decoder.Seek(std::max(0.0, seek_seconds)) — BEST-EFFORT: Seek
    //    clamps out-of-range itself; on false (some containers refuse
    //    seeks) do NOT fail — the decoder sits at position ~0 and the
    //    bounded forward loop below still yields a valid (early) frame.
    //    A wrong-position poster beats no poster; catalog can re-extract.
    // 5. Forward-discard: loop up to 300 NextFrame calls; keep the LAST
    //    successfully decoded frame whose GetPositionSeconds() >=
    //    seek_seconds; stop at first such frame; on EOF without reaching
    //    target keep the last decoded frame. Implementation:
    //      std::vector<uint8_t> bgra; int w=0,h=0; bool got=false;
    //      for (int i=0;i<300 && decoder.NextFrame(bgra,w,h);++i) {
    //          got=true;
    //          if (decoder.GetPositionSeconds() >= seek_seconds) break;
    //      }
    //    (NextFrame resizes bgra; copy into video::Frame each iteration
    //     or only the kept one — copy only when breaking/at end to avoid
    //     per-frame alloc: simplest correct = copy on every iter into a
    //     video::Frame `best` — 300 small copies max, fine.)
    // 6. video::Frame frame; frame.Allocate(w,h,video::PixelFormat::BGRA8);
    //    std::memcpy(frame.Data(), bgra.data(), bgra.size()); — assert
    //    bgra.size() == w*h*4 first.
    // 7. video::FrameExporter exporter; return exporter.ToFile(
    //    utf8_out_png_path, frame) — on false set error_out generic.
}
```

Non-Windows: `VideoDecoder::Open` is a stub returning false → step 2
fails cleanly; `FrameExporter::ToFile` double-checks
`IsAvailable()`. No `#ifdef` needed in clip_poster.cc.

- [ ] **Step 4: Build + run — verify poster tests PASS**

Same commands. Expected: PNG written for video, clean false for
wav/missing/clamps.

- [ ] **Step 5: Commit**

```bash
git add tools/video_editor/clip_poster.{h,cc} tests/video/video_editor_poster_tests.cc
git commit -m "Add ClipPoster mid-clip frame extraction for VideoEditor"
```

---

## Task 5: `media_catalog` + audio-only `ProbeVideo` relaxation

**Files:**
- Create: `tools/video_editor/media_catalog.h`,
  `tools/video_editor/media_catalog.cc`
- Modify: `src/content/video/video_info.cc` (relax audio-only — ~15
  lines) and `src/content/video/video_info.h` (doc already promises
  audio-only success — verify wording, likely no change needed)
- Modify: `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc` (opportunistic
  1-line `EnableAudioTap` non-Windows fix — see step 4c)
- Modify: `tests/video/video_editor_catalog_tests.cc` (replace stub)
- Possibly modify: `tests/CMakeLists.txt` — NO new exe needed (same
  `XER_VideoEditor_Tests`); but the catalog tests exercise
  `XER_ContentVideo`/`XER_FFmpeg` symbols — already PUBLIC-linked via
  `XER_VideoEditor_Core`.

**Interfaces:**
- Consumes: `XER::Content::Video::ProbeVideo` + `VideoInfo`
  (`video_info.h`), `XER::SQL::Storage::SQLiteDatabase`,
  `ClipPoster::Extract` (Task 4), `XER::Helper::Path::*` +
  `GetEnv`.
- Produces:

```cpp
struct CatalogEntry {
    std::int64_t id = 0;
    std::string path;                 // canonical UTF-8 absolute
    double duration_seconds = 0;
    int width = 0, height = 0;        // 0/0 = audio-only
    double fps = 0;
    std::string video_codec, audio_codec;
    bool has_audio = false;
    std::int64_t file_size_bytes = 0;
    std::int64_t mtime_unix = 0;
    std::string probe_json;           // serialized VideoInfo fields
    std::string poster_path;          // UTF-8, empty when none
    std::int64_t added_at_unix = 0;
    bool HasVideo() const { return width > 0 && height > 0; }
};

class MediaCatalog {
public:
    // utf8_db_path empty → DefaultDbPath(). Single connection,
    // UI-thread-owned — NOT internally synchronized (callers serialize,
    // same contract as NodeGraph::ProjectStore).
    explicit MediaCatalog(std::string utf8_db_path = {});
    ~MediaCatalog();
    MediaCatalog(const&) = delete;
    MediaCatalog& operator=(const&) = delete;

    bool Open(std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;
    const std::string& DbPath() const { return db_path_; }

    static std::string DefaultDbPath();     // see env chain below
    // Posters live in <db_dir>/posters/<id>.png — "next to the DB".

    // Probe + upsert by canonical path. extract_poster && width>0 →
    // ClipPoster::Extract at duration*0.5 into posters/<id>.png and
    // store poster_path; poster failure is NON-fatal (row still
    // upserts, poster_path keeps its previous/empty value).
    bool Import(const std::string& utf8_path, std::int64_t* id_out = nullptr,
                bool extract_poster = true, std::string* error_out = nullptr);

    bool FindByPath(const std::string& utf8_path, CatalogEntry* out,
                    std::string* error_out = nullptr) const;
    bool FindById(std::int64_t id, CatalogEntry* out,
                  std::string* error_out = nullptr) const;
    std::vector<CatalogEntry> List(std::string* error_out = nullptr) const;
    // Deletes the row; best-effort deletes the poster file when
    // remove_poster_file (default true).
    bool Remove(std::int64_t id, bool remove_poster_file = true,
                std::string* error_out = nullptr);

private:
    bool EnsureSchema(std::string* error_out);
    std::string PosterDir() const;
    std::string db_path_;
    std::unique_ptr<XER::SQL::Storage::SQLiteDatabase> db_;
};
```

`DefaultDbPath()` env chain (UTF-8 safe):
`NormalizeUtf8Path(GetEnv("LOCALAPPDATA"))` → if empty
`NormalizeUtf8Path(GetEnv("XDG_DATA_HOME"))` → if empty
`NormalizeUtf8Path(GetEnv("HOME")) + "/.local/share"` → if empty
`temp_directory_path()`; append `/XER/VideoEditor/catalog.db` (generic
`/` separators — SQLite/Windows accept them; keeps code paths simple).

Schema (spec columns + NOCASE path dedup — deliberate hardening for
Windows case-insensitive FS):

```sql
CREATE TABLE IF NOT EXISTS media (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL COLLATE NOCASE,
    duration REAL NOT NULL DEFAULT 0,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0,
    fps REAL NOT NULL DEFAULT 0,
    video_codec TEXT NOT NULL DEFAULT '',
    audio_codec TEXT NOT NULL DEFAULT '',
    has_audio INTEGER NOT NULL DEFAULT 0,
    file_size INTEGER NOT NULL DEFAULT 0,
    mtime INTEGER NOT NULL DEFAULT 0,
    probe_json TEXT NOT NULL DEFAULT '',
    poster_path TEXT NOT NULL DEFAULT '',
    added_at INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS media_path_uq ON media(path);
```

(`COLLATE NOCASE` + unique index ⇒ `D:/a.MP4`/`d:/a.mp4` dedup;
canonicalization resolves `.`/`..`. Non-ASCII case edges are accepted
— NOCASE folds ASCII only.)

- [ ] **Step 1: Write the failing catalog tests**

Replace stub with `int XER_VideoEditor_RunCatalogTests()`:

```cpp
// Cases:
// 0. DefaultDbPath() returns a non-empty path ending in
//    "catalog.db" (no filesystem writes — pure string check).
// 1. Open(tempdir/catalog.db) creates parent dirs + schema; reopen a
//    second MediaCatalog on same path → IsOpen, List() empty.
// 2. Import mp4 fixture (EncodeVideoFrameStream-generated, gated on
//    IsAvailable): entry has width/height>0, duration>0, has_audio or
//    video_codec non-empty, file_size>0, mtime>0, probe_json non-empty;
//    extract_poster=true → poster_path non-empty AND file exists with
//    PNG magic; id_out > 0.
// 3. Re-Import same path (case-variant "D:/.../A.MP4" vs lowercase on
//    Windows — or just same path twice): SAME id returned, row count
//    stays 1, added_at preserved (capture first added_at, re-import,
//    compare).
// 4. Import audio-only wav fixture → width==0, has_audio true, video_
//    codec empty; extract_poster skipped (poster_path empty).
// 5. FindByPath case-variant returns the row; FindById round-trips;
//    List() ordering by id.
// 6. Remove(id) → FindById false; poster file gone from disk.
// 7. Import nonexistent path → false + error; Import a text file →
//    ProbeVideo fails → false.
// 8. UTF-8 path: copy fixture to tempdir/"медиа 🚬.mp4"-style name
//    (Cyrillic + emoji — valid on NTFS) → Import → FindByPath with the
//    same UTF-8 string returns it; stored path round-trips byte-exact.
```

- [ ] **Step 2: Build — verify compile error (RED)**

`"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests` → `media_catalog.h` not found.

- [ ] **Step 3: Implement `media_catalog.{h,cc}`**

Follow `node_graph_project_store.cc` patterns exactly:

- Anonymous-namespace helpers `VStr/VInt/VReal/VBool` +
  `AsString/AsInt/AsReal/AsBool` over `XER::SQL::Content::Value`.
- `kSchemaStatements[]` — separate `Execute` calls (single-statement
  rule).
- `Open`: `create_directories(parent_path(db_path_))` →
  `SQLiteDatabase::Open` → `EnsureSchema`.
- `Import` sequence:
  1. `std::string canon = ToUtf8(weakly_canonical(FromUtf8(
     NormalizeUtf8Path(path))))` — absolute, `.`/`..` resolved,
     UTF-8 out. `weakly_canonical` on a nonexistent path still returns
     a normalized path (ProbeVideo then reports not-found).
  2. `XER::Content::Video::VideoInfo info; ProbeVideo(canon, &info,
     error)` — false → return false (covers audio-only AFTER Task 5's
     relaxation; before it, audio-only import fails — see step 4).
  3. `mtime_unix`: `fs::last_write_time(FromUtf8(canon))` →
     `std::chrono::clock_cast<std::chrono::system_clock>(ftime)` →
     `duration_cast<seconds>` (MSVC/C++20 OK); `file_size` prefer
     `info.file_size_bytes`, fallback `fs::file_size`.
  4. `probe_json`: `Json::Value` object {path,width,height,fps,
     duration_seconds,video_codec,audio_codec,has_audio,
     file_size_bytes} → `Json::writeString(StreamWriterBuilder,
     compact)`.
  5. Upsert: `UPDATE media SET duration=?,width=?,height=?,fps=?,
     video_codec=?,audio_codec=?,has_audio=?,file_size=?,mtime=?,
     probe_json=? WHERE path=?` → if `Changes()==0` → `INSERT INTO
     media(path,...,added_at) VALUES(...,?)` with `added_at=now`.
     Then `SELECT id FROM media WHERE path=?` → id.
  6. Poster (non-fatal): if `extract_poster && info.width > 0`:
     `seek = info.duration_seconds > 0 ? info.duration_seconds*0.5 :
     0.0`; `ClipPoster::Extract(canon, seek, PosterDir()+"/"+
     to_string(id)+".png", nullptr)` → on success `UPDATE media SET
     poster_path=? WHERE id=?`.
  7. `*id_out = id`; return true.
- `FindByPath`: canonicalize arg the same way → `SELECT <cols> FROM
  media WHERE path=?` (column `COLLATE NOCASE` makes `=` case-
  insensitive). `FindById`: `WHERE id=?`. `List`: `SELECT <cols> FROM
  media ORDER BY id`. Row→`CatalogEntry` mapper shared.
- `Remove`: `SELECT poster_path` → `DELETE FROM media WHERE id=?` →
  if `remove_poster_file && !poster_path.empty()` →
  `fs::remove(FromUtf8(poster_path), ec)` best-effort.
- `PosterDir()`: `fs::path(db_path_).parent_path()/"posters"` as UTF-8
  via `ToUtf8`; `Import` creates it before extracting.

- [ ] **Step 4: Relax `ProbeVideo` for audio-only + `EnableAudioTap` fix**

**4a. `src/content/video/video_info.cc`** — the header
(`video_info.h:23-26`) ALREADY documents "An audio-only file probes
successfully but reports width/height = 0"; the code contradicts it
(`video_info.cc:34-37` returns "no video stream"). Fix the code to match
the documented contract:

```cpp
    if (video == nullptr) {
        if (audio == nullptr) {
            if (error_out != nullptr)
                *error_out = "no decodable audio/video streams";
            return false;
        }
        // Audio-only container: succeed with width/height = 0 —
        // matches the header contract and the decoder's audio-only
        // mode. Callers that need pictures check those fields.
    }
```

Then guard the video-only fields: `info->width/height` stay 0 (already
defaulted); `video_codec` = `video ? video->codec_name : ""`;
`has_audio = audio != nullptr`; `audio_codec` likewise; duration:
container `media.duration` first, else fall back to the AUDIO stream's
`duration`/`time_base` when `video == nullptr` (mirror the existing
formula with `audio` in place of `video`); fps stays 0 (the
`video->frame_count` block is skipped when `video == nullptr`).
`file_size`/`valid` unchanged.

Impact note (document in commit message): `VideoSession::Open` now
accepts audio-only files — `DecodeLoop` opens the (audio-only-capable)
decoder, publishes kReady, and reaches kEnded on first play tick; the
audio tap still receives packets while `NextFrame()` pumps. Preview of
audio-only clips is a 1D concern (spec: Bridge decoder +
`VideoAudioPlayer`, NOT `VideoEngine`) — no 1B consumer regresses.

**4b. Catalog test for audio-only import** — the wav fixture case in
step 1 now passes through the relaxed probe (width=0 row).

**4c. Opportunistic `EnableAudioTap` non-Windows fix** —
`src/wrapper/ffmpeg/ffmpeg_video_decoder.cc:256`: `struct Dummy {};`
has no `audio_tap` member but `EnableAudioTap` (line 267-269) assigns
`impl_->audio_tap` unconditionally → non-Windows compile break. One-
line fix: `struct Dummy { bool audio_tap = false; };` — keeps the
method symmetric, zero behavior change on Windows. (Ride along; ~3
lines total with the assignment staying as-is.)

- [ ] **Step 5: Build + run — verify catalog tests PASS**

Same commands. Expected: all catalog cases pass including audio-only
import and unicode path; `XER_Video_Smoke`/`XER_UI_Tests` unaffected
(re-run `ctest -R "XER_Video|XER_UI"` as regression spot-check — the
decoder Dummy change is inert on MSVC, ProbeVideo relaxation only
widens acceptance).

- [ ] **Step 6: Commit**

```bash
git add tools/video_editor/media_catalog.{h,cc} \
        src/content/video/video_info.cc \
        src/wrapper/ffmpeg/ffmpeg_video_decoder.cc \
        tests/video/video_editor_catalog_tests.cc
git commit -m "Add VideoEditor media catalog; accept audio-only in ProbeVideo"
```

---

## Task 6: Full verification & wrap-up

**Files:** none new — verification only.

- [ ] **Step 1: Rebuild everything affected**

Run: `"D:/Developers/XER3/out/vcenv.bat" cmake --build out/build/default --target XER_VideoEditor_Tests XER_Video_Smoke XER_UI_Tests XER_ContentVideo`
Expected: zero warnings-as-errors (`/W4`), clean build.

- [ ] **Step 2: Run the full test suite**

Run: `"D:/Developers/XER3/out/vcenv.bat" ctest --test-dir out/build/default --output-on-failure`
Expected: `XER_VideoEditor_Tests` (all five suites), `XER_Video_Smoke`,
`XER_UI_Tests`, `XER_SecurityFlash_Smoke`, `XER_ContentApps_Smoke` all
pass. Spot-run `out/build/default/bin/XER_VideoEditor_Tests.exe`
directly to see per-check output.

- [ ] **Step 3: Final review pass**

- `git status` clean except intended files; `git log` shows the five
  task commits on top of `a0f5058`.
- Confirm no FFmpeg headers leaked: `grep -rn "libav\|libsw\|libyuv"
  tools/video_editor/ tests/video/` → comments only.
- Confirm `XER_VideoEditor_Core` is the ONLY new lib and has no exe
  consumer yet (by design — 1C adds `video_editor` exe).
- Update `docs/wiki/architecture/video-pipeline-facts.md` "Phase-1A
  additions" section (or add a "Phase-1B" note): ProbeVideo now accepts
  audio-only; `EnableAudioTap` Dummy fix; catalog/poster paths; known
  remaining handoffs (Skia BlendSrcOver → 1C, Seek av_rescale_q → 1C,
  position_seconds audio-only → 1C, atomic mux temp→commit → 1C).
- Spec deviations to record in the wiki/plan report: poster files are
  `.png` (not `.jpg`); `Transform.offset` convention = top-left
  fraction of canvas; snap = query helpers (`CollectSnapPoints`/
  `SnapTime`), not a history command; `RemoveTrack` reindexes
  `clip.track` (spec's index-based tracks require it).

- [ ] **Step 4: Commit stragglers** (wiki updates; `docs/` is
  untracked in this repo — leave untracked unless the parent asks).

---

## Spec gaps resolved in this plan (flag if you disagree)

- **Snap is not a mutation**: plan-index lists "snap" among history
  operations, but snapping only chooses a drag target — implemented as
  model queries `CollectSnapPoints`/`SnapTime`; the UI issues a normal
  `MoveClip`/`TrimClip` command with the snapped value. No `SnapCommand`.
- **`Transform.offset_*` reference point**: spec says "fraction of
  canvas w/h" without saying from where — defined as the clip's
  top-left corner in canvas fractions; `FitTransformToCanvas` produces
  centered-letterbox values under that convention. 1C's compositor MUST
  implement the same convention (document in its plan).
- **Clip `id` generation**: spec says uuid — `NewClipId()` (uuid v4,
  `std::random_device`, no platform deps) in `timeline_model`; history
  generates once per command so redo is deterministic.
- **Poster format**: `.png` via `FrameExporter::ToFile` (spec's `<id>.jpg`
  was illustrative; `ImageToJpeg` can't seek — PNG is the spec'd
  pipeline).
- **`ProbeVideo` audio-only**: relaxed to match its own header contract
  (was self-contradictory); catalog consumes it directly — no parallel
  probe path. `VideoSession` now accepts audio-only files (degraded
  preview: kEnded on play) — acceptable, 1D uses the Bridge-decoder
  path per spec anyway.
- **Track removal**: spec fixes `clip.track` as an INDEX — `RemoveTrack`
  reindexes `clip.track > removed`; history captures affected clips so
  undo restores exact prior indices.
- **Split fades**: fade_in stays on first half / fade_out on second
  (fades anchor to clip start/end); gain/mute/transform/effects copy
  verbatim to both halves.
- **Transition self-heal on load**: `.xerproj` transitions referencing
  missing/non-adjacent clips are dropped by `DropInvalidTransitions`
  after parse (consistent with the "invalid transitions dropped"
  invariant); primary clip data errors still fail the load.
- **Catalog path dedup**: `path TEXT COLLATE NOCASE` + unique index —
  case-insensitive on Windows; canonical UTF-8 via
  `weakly_canonical`+`NormalizeUtf8Path`.
- **Test placement**: `tests/video/` + dedicated `XER_VideoEditor_Tests`
  exe (plain-assert, header-only shared harness, one main dispatching
  per-suite `Run*Tests()`) — NOT the `XER_UI_TEST` harness (keeps
  UI-free core tests out of the UI exe; matches `xer_video_smoke.cc`
  precedent in the same directory).
