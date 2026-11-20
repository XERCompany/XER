// Implementation of smoke_checks.h — see the header for the contract.
// Each check returns an empty string on success or a one-line failure
// reason. Keep every check synchronous and cheap: the whole suite is
// expected to finish in a few seconds; the watchdog in RunAll() is
// the hard cap, not the budget.
#include "smoke_checks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "audio_mixer.h"
#include "compositor.h"
#include "export_engine.h"
#include "export_preset.h"
#include "generated_source.h"
#include "helper/path_utils.h"
#include "keyboard_map.h"
#include "media_catalog.h"
#include "project_autosave.h"
#include "project_store.h"
#include "sequence_preset.h"
#include "source_edit.h"
#include "timecode.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::VideoEditor::Smoke {
namespace {

namespace Path = XER::Helper::Path;
namespace FFmpeg = XER::Bridge::FFmpeg;

// Hard cap for the entire run — a hung check must not block callers.
constexpr int kWatchdogSeconds = 15;

// Scratch area for the file-backed checks (store, autosave, catalog,
// presets, export). Distinct from the unit-test dir so a smoke run
// never collides with XER_VideoEditor_Tests fixtures.
std::filesystem::path SmokeTempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_video_editor_smoke";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

Clip MakeGeneratedClip(const std::string& id, int track, double start,
                       double duration, SourceKind kind,
                       const std::string& param) {
    Clip clip;
    clip.id = id;
    clip.track = track;
    clip.timeline_start = start;
    clip.duration = duration;
    clip.source.kind = kind;  // source_path stays empty — no media file
    clip.source.param = param;
    return clip;
}

// Small all-procedural project shared by the render-path checks:
// one generated bars clip on V1 plus an empty A1 — no decoder is ever
// opened, so these checks are truly headless.
Project MakeRenderProject(int width, int height, int fps_num) {
    Project project;
    project.width = width;
    project.height = height;
    project.fps_num = fps_num;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 0.0, 1.0, SourceKind::ColorBars, "smpte75"));
    return project;
}

// ---- checks ---------------------------------------------------------

// Assets-layer boot: app.yaml + V8 runtime.js + window.yaml. This is
// the same path RunGui takes before creating the window — it catches
// a broken/missing assets deploy, a dead JS engine, and malformed
// yaml without needing a visible window.
std::string CheckAppBoot() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);
    if (!boot.ok) {
        return boot.error.empty() ? "assets boot failed"
                                  : "assets boot failed: " + boot.error;
    }
    if (boot.window.width <= 0 || boot.window.height <= 0) {
        return "window config has non-positive size";
    }
    // The BootResult owns the engine; clear the published pointer
    // before it dies so nothing dereferences a stale JsEngine.
    AppRuntime::SharedBootEngine() = nullptr;
    return {};
}

std::string CheckTimecode() {
    for (const double seconds : {0.0, 1.5, 61.04, 3661.5}) {
        const std::string text = FormatTimecode(seconds, 30, 1);
        if (text.size() != 11 || text[2] != ':' || text[5] != ':' ||
            text[8] != ':') {
            return "FormatTimecode produced '" + text + "'";
        }
        double parsed = -1.0;
        if (!ParseTimecode(text, 30, 1, &parsed)) {
            return "ParseTimecode rejected '" + text + "'";
        }
        const std::int64_t want = SecondsToFrames(seconds, 30, 1);
        const std::int64_t got = ParseToFrames(text, 30, 1);
        if (got != want) {
            return "round trip mismatch for '" + text + "'";
        }
    }
    if (ParseTimecode("not-a-timecode", 30, 1, nullptr) ||
        ParseTimecode("00:00:61:00", 30, 1, nullptr)) {
        return "accepted a malformed timecode";
    }
    return {};
}

std::string CheckSequencePresets() {
    const auto presets = BuiltinSequencePresets();
    if (presets.size() < 3) {
        return "too few builtin sequence presets";
    }
    for (const auto& preset : presets) {
        Project project;
        if (!ApplySequencePreset(preset, &project)) {
            return "ApplySequencePreset rejected '" + preset.name + "'";
        }
        if (project.width != preset.w || project.height != preset.h ||
            project.fps_num != preset.fps_num ||
            project.fps_den != preset.fps_den) {
            return "ApplySequencePreset misapplied '" + preset.name + "'";
        }
    }
    const SequencePreset found =
        FindPreset(presets.front().w, presets.front().h,
                   presets.front().fps_num, presets.front().fps_den);
    if (found.name.empty()) {
        return "FindPreset missed a builtin geometry";
    }
    return {};
}

std::string CheckTimelineModel() {
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;

    if (AddTrack(project, TrackKind::Video, "V1") != 0 ||
        AddTrack(project, TrackKind::Audio, "A1") != 1) {
        return "AddTrack returned unexpected indices";
    }

    const MediaBounds media{60.0, true};
    Clip clip;
    clip.id = "smoke-c1";
    clip.source_path = "clip.mp4";
    clip.track = 0;
    clip.duration = 5.0;
    if (!AddClip(project, clip, media, &err)) {
        return "AddClip rejected a valid clip: " + err;
    }

    // Invariant checks: overlap and audio-on-video must be rejected
    // transactionally (the project keeps its one clip).
    Clip overlap = clip;
    overlap.id = "smoke-c2";
    overlap.timeline_start = 2.0;
    if (AddClip(project, overlap, media, nullptr)) {
        return "AddClip accepted an overlapping clip";
    }
    if (project.clips.size() != 1) {
        return "rejected AddClip still mutated the project";
    }
    Clip audio_only = clip;
    audio_only.id = "smoke-c3";
    audio_only.timeline_start = 10.0;
    const MediaBounds audio_bounds{60.0, false};
    if (AddClip(project, audio_only, audio_bounds, nullptr)) {
        return "AddClip placed audio-only media on a video track";
    }

    if (!MoveClip(project, "smoke-c1", 10.0, media, &err) ||
        FindClip(project, "smoke-c1")->timeline_start != 10.0) {
        return "MoveClip failed: " + err;
    }
    if (!SplitClip(project, "smoke-c1", 12.0, "smoke-c1b", &err) ||
        project.clips.size() != 2) {
        return "SplitClip failed: " + err;
    }
    Marker marker;
    marker.time = 3.0;
    marker.name = "smoke";
    if (!AddMarker(project, marker, &err) ||
        !SetWorkArea(project, 1.0, 4.0, &err)) {
        return "marker/work-area op failed: " + err;
    }
    if (project.duration() != 15.0) {
        return "project duration mismatch after edits";
    }
    return {};
}

std::string CheckTimelineHistory() {
    Project project;
    TimelineHistory history(project);
    std::string err;

    if (history.AddTrack(TrackKind::Video, "V1") != 0) {
        return "history AddTrack failed";
    }
    Clip clip;
    clip.id = "smoke-h1";
    clip.source_path = "clip.mp4";
    clip.track = 0;
    clip.duration = 4.0;
    const MediaBounds media{30.0, true};
    if (!history.AddClip(clip, media, nullptr, &err)) {
        return "history AddClip failed: " + err;
    }
    if (!history.CanUndo() || history.UndoLabel().empty()) {
        return "history did not record the add";
    }
    if (!history.Undo() || !project.clips.empty()) {
        return "Undo did not remove the clip";
    }
    if (!history.CanRedo() || !history.Redo() ||
        project.clips.size() != 1) {
        return "Redo did not restore the clip";
    }
    return {};
}

std::string CheckSourceEdit() {
    // Headless three-point-edit path: marked source in/out resolves to
    // a Clip whose in_point/duration land through the model's
    // insert/overwrite ops — the same calls the source monitor's ,/.
    // keys dispatch (through TimelineHistory in the app).
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;
    if (AddTrack(project, TrackKind::Video, "V1") != 0) {
        return "AddTrack failed";
    }
    const MediaBounds media{20.0, true};
    Clip base;
    base.id = "base";
    base.source_path = "a.mp4";
    base.track = 0;
    base.timeline_start = 0.0;
    base.duration = 8.0;
    if (!AddClip(project, base, media, &err)) {
        return "AddClip failed: " + err;
    }

    // marks 2.0 -> 5.5 on a 20 s clip: resolves verbatim.
    SourceMarks marks;
    marks.in = 2.0;
    marks.out = 5.5;
    double src_in = 0.0, src_out = 0.0;
    if (!ResolveSourceRange(marks, 20.0, &src_in, &src_out) ||
        src_in != 2.0 || src_out != 5.5) {
        return "ResolveSourceRange mis-resolved marked range";
    }
    Clip edit = MakeSourceClip("b.mp4", 0, 10.0, src_in, src_out,
                               640, 360, 320, 180);
    if (edit.in_point != 2.0 || edit.duration != 3.5) {
        return "MakeSourceClip produced the wrong range";
    }
    edit.id = "edit";
    // Insert at t=10 (past the base clip): no straddler to split.
    if (!SourceEditAt(project, edit, media, true, &err)) {
        return "insert edit failed: " + err;
    }
    if (project.clips.size() != 2 ||
        FindClip(project, "edit") == nullptr) {
        return "inserted clip missing";
    }
    // Overwrite over the base clip's middle: [2,5.5) replaces its span.
    Clip over = MakeSourceClip("c.mp4", 0, 2.0, 3.0, 6.0,
                               640, 360, 320, 180);
    over.id = "over";
    if (!SourceEditAt(project, over, media, false, &err)) {
        return "overwrite edit failed: " + err;
    }
    const Clip* placed = FindClip(project, "over");
    if (placed == nullptr || placed->in_point != 3.0 ||
        placed->duration != 3.0) {
        return "overwrote clip has the wrong source range";
    }
    // Unmarked resolve falls back to the whole media duration.
    if (!ResolveSourceRange(SourceMarks{}, 7.0, &src_in, &src_out) ||
        src_in != 0.0 || src_out != 7.0) {
        return "unmarked range did not span the media";
    }
    // An inverted range (out <= in) must be rejected, not placed.
    SourceMarks bad;
    bad.in = 5.0;
    bad.out = 5.0;
    if (ResolveSourceRange(bad, 20.0, &src_in, &src_out)) {
        return "empty marked range resolved";
    }
    return {};
}

std::string CheckKeyboardMap() {
    const auto& shortcuts = EditorShortcuts();
    if (shortcuts.size() < 5) {
        return "shortcut table suspiciously small";
    }
    for (const auto& entry : shortcuts) {
        if (entry.group.empty() || entry.keys.empty() ||
            entry.action.empty()) {
            return "shortcut row has an empty field";
        }
    }
    return {};
}

std::string CheckExportPresets() {
    const auto presets = BuiltinPresets();
    if (presets.size() < 2) {
        return "too few builtin export presets";
    }
    ExportOptions options;
    ApplyPreset(presets.front(), &options);
    if (options.video_codec != presets.front().video_codec ||
        options.video_bit_rate != presets.front().video_bit_rate ||
        options.audio_codec != presets.front().audio_codec) {
        return "ApplyPreset did not stamp the preset fields";
    }

    const std::string path =
        Utf8Path(SmokeTempDir() / "export_presets.json");
    std::string err;
    if (!SavePresets(presets, path, &err)) {
        return "SavePresets failed: " + err;
    }
    std::vector<ExportPreset> loaded;
    if (!LoadPresets(path, &loaded, &err) ||
        loaded.size() != presets.size() ||
        loaded.front().name != presets.front().name) {
        return "preset round trip mismatch: " + err;
    }
    // Missing file is not an error by contract — first-run tolerance.
    std::vector<ExportPreset> none;
    if (!LoadPresets(path + ".missing", &none, nullptr) ||
        !none.empty()) {
        return "LoadPresets failed on a missing file";
    }
    return {};
}

std::string CheckGeneratedSource() {
    // BGRA: pure red parses to {0,0,255,255}; garbage falls back to
    // opaque black — both contracts are load-bearing for .xerproj
    // files in the wild.
    if (ParseMatteColor("#FF0000") !=
        std::array<std::uint8_t, 4>{0, 0, 255, 255}) {
        return "ParseMatteColor mis-parsed #FF0000";
    }
    if (ParseMatteColor("junk") !=
        std::array<std::uint8_t, 4>{0, 0, 0, 255}) {
        return "ParseMatteColor did not fall back to black";
    }

    video::Frame frame;
    Clip bars = MakeGeneratedClip("bars", 0, 0.0, 1.0,
                                  SourceKind::ColorBars, "smpte75");
    if (!GenerateSourceFrame(bars, 0.0, 160, 90, &frame) ||
        !frame.IsValid() || frame.Width() != 160 ||
        frame.Height() != 90) {
        return "ColorBars frame generation failed";
    }
    Clip title = MakeGeneratedClip("title", 0, 0.0, 1.0,
                                   SourceKind::Text, "Smoke | size=24");
    video::Frame text_frame;
    if (!GenerateSourceFrame(title, 0.0, 160, 90, &text_frame) ||
        !text_frame.IsValid()) {
        return "Text frame generation failed";
    }
    return {};
}

std::string CheckCompositorRender() {
    Project project = MakeRenderProject(160, 90, 15);
    Compositor compositor;
    std::string err;
    if (!compositor.Init(project, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    if (bgra.size() != static_cast<std::size_t>(160) * 90 * 4) {
        return "rendered buffer has the wrong size";
    }
    const bool any_pixels =
        std::any_of(bgra.begin(), bgra.end(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "rendered frame is all zeros";
    }
    return {};
}

std::string CheckAudioMixer() {
    // The bars clip owns no audio stream, so the correct output is
    // well-defined silence — the check exercises chunk production,
    // the s16-stereo contract, and the no-decoder path end to end.
    Project project = MakeRenderProject(160, 90, 15);
    AudioMixer mixer;
    AudioMixerOptions options;
    options.chunk_samples = 1024;
    std::string err;
    if (!mixer.Init(project, options, &err)) {
        return "AudioMixer::Init failed: " + err;
    }
    if (mixer.ChunkCount() < 1) {
        mixer.Shutdown();
        return "ChunkCount is zero for a 1 s project";
    }
    FFmpeg::AudioFrameInfo chunk;
    const bool got = mixer.GetChunk(0, &chunk, &err);
    mixer.Shutdown();
    if (!got) {
        return "GetChunk(0) failed: " + err;
    }
    if (chunk.sample_rate != 48000 || chunk.channels != 2 ||
        chunk.planar || chunk.sample_count <= 0) {
        return "chunk violates the s16-stereo contract";
    }
    if (chunk.data.size() !=
        static_cast<std::size_t>(chunk.sample_count) * 2 * 2) {
        return "chunk data size mismatch";
    }
    const bool silent =
        std::all_of(chunk.data.begin(), chunk.data.end(),
                    [](std::uint8_t byte) { return byte == 0; });
    if (!silent) {
        return "decoderless project produced non-silent audio";
    }
    return {};
}

std::string CheckProjectStore() {
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 24;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.push_back(MakeGeneratedClip(
        "matte", 0, 0.0, 2.0, SourceKind::ColorMatte, "#2244CC"));
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 2.0, 1.5, SourceKind::ColorBars, "smpte75"));
    Marker marker;
    marker.time = 1.0;
    marker.name = "smoke-marker";
    project.markers.push_back(marker);
    project.work_in = 0.5;
    project.work_out = 3.0;
    project.transitions.push_back(Transition{
        "matte", "bars", Transition::Type::Crossfade, 0.5});

    const std::string path =
        Utf8Path(SmokeTempDir() / "smoke_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    if (loaded.width != project.width ||
        loaded.height != project.height ||
        loaded.fps_num != project.fps_num ||
        loaded.tracks.size() != project.tracks.size() ||
        loaded.clips.size() != project.clips.size() ||
        loaded.markers.size() != 1 ||
        loaded.work_in != project.work_in ||
        loaded.work_out != project.work_out) {
        return "round trip field mismatch";
    }
    const Clip* clip = FindClip(loaded, "bars");
    if (clip == nullptr || clip->source.kind != SourceKind::ColorBars ||
        clip->timeline_start != 2.0 || clip->duration != 1.5) {
        return "clip fields did not survive the round trip";
    }
    // Adjacent matte->bars means the transition must be preserved
    // rather than dropped by load-time self-healing.
    if (loaded.transitions.size() != 1 ||
        loaded.transitions.front().from_clip != "matte") {
        return "transition did not survive the round trip";
    }
    return {};
}

std::string CheckProjectAutosave() {
    const auto dir = SmokeTempDir();
    const std::string project_path = Utf8Path(dir / "autosave_me.xerproj");
    const std::string autosave_dir = Utf8Path(dir / "autosaves");

    Project project;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 0.0, 1.0, SourceKind::ColorBars, "smpte75"));

    AutosaveManager manager;
    std::string err;
    if (!manager.Init(project_path, autosave_dir, &err)) {
        return "Init failed: " + err;
    }
    manager.SetIntervalMs(0);  // always due
    manager.MarkDirty();
    if (!manager.MaybeAutosave(project, 1000, &err)) {
        return "MaybeAutosave failed: " + err;
    }
    if (manager.ListAutosaves().empty()) {
        return "no autosave slot was written";
    }
    Project recovered;
    if (!manager.LoadLatest(&recovered, &err)) {
        return "LoadLatest failed: " + err;
    }
    if (FindClip(recovered, "bars") == nullptr) {
        return "recovered project lost its clip";
    }
    return {};
}

std::string CheckMediaCatalog() {
    const std::string db_path =
        Utf8Path(SmokeTempDir() / "media_catalog.db");
    MediaCatalog catalog(db_path);
    std::string err;
    if (!catalog.Open(&err) || !catalog.IsOpen()) {
        return "Open failed: " + err;
    }
    err.clear();
    const auto entries = catalog.List(&err);
    if (!err.empty()) {
        catalog.Close();
        return "List failed: " + err;
    }
    if (!entries.empty()) {
        catalog.Close();
        return "fresh catalog is not empty";
    }
    // Negative paths must fail cleanly, not crash or fabricate rows.
    CatalogEntry entry;
    if (catalog.FindByPath("definitely/missing.mp4", &entry)) {
        catalog.Close();
        return "FindByPath found a phantom entry";
    }
    std::int64_t id = -1;
    if (catalog.Import("definitely/missing.mp4", &id, false)) {
        catalog.Close();
        return "Import accepted a missing file";
    }
    catalog.Close();
    return {};
}

// End-to-end: compositor -> libx264 -> mp4 mux via the real FFmpeg
// bridge, all on procedural clips — no media fixture required. Kept
// last since it is the slowest check (~1 s typical).
std::string CheckExportRender() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const auto output = SmokeTempDir() / "smoke_export.mp4";
    {
        std::error_code ec;
        std::filesystem::remove(output, ec);
    } 
    ExportOptions options;
    options.output_path = Utf8Path(output);
    options.video_bit_rate = 200000;

    ExportEngine engine;
    std::string err;
    if (!engine.Start(MakeRenderProject(64, 64, 15), options, {},
                      &err)) {
        return "Start failed: " + err;
    }
    engine.Wait();
    if (engine.Status() != ExportStatus::kDone) {
        return "export ended in state " +
               std::to_string(static_cast<int>(engine.Status())) +
               ": " + engine.Error();
    }
    std::error_code ec;
    if (!std::filesystem::exists(output, ec) ||
        std::filesystem::file_size(output, ec) == 0) {
        return "export produced no output file";
    }
    return {};
}

// ---- registry + runner ----------------------------------------------

using CheckFn = std::string (*)();
struct CheckEntry {
    const char* name;
    CheckFn fn;
};

// Order is part of the contract --smoke-list advertises. Cheap pure
// checks first; the heavyweight boot and FFmpeg export go last.
constexpr CheckEntry kChecks[] = {
    {"timecode", &CheckTimecode},
    {"sequence_presets", &CheckSequencePresets},
    {"timeline_model", &CheckTimelineModel},
    {"timeline_history", &CheckTimelineHistory},
    {"source_edit", &CheckSourceEdit},
    {"keyboard_map", &CheckKeyboardMap},
    {"export_presets", &CheckExportPresets},
    {"generated_source", &CheckGeneratedSource},
    {"compositor_render", &CheckCompositorRender},
    {"audio_mixer", &CheckAudioMixer},
    {"project_store", &CheckProjectStore},
    {"project_autosave", &CheckProjectAutosave},
    {"media_catalog", &CheckMediaCatalog},
    {"app_boot", &CheckAppBoot},
    {"export_render", &CheckExportRender},
};

}  // namespace

std::vector<std::string> CheckNames() {
    std::vector<std::string> names;
    names.reserve(std::size(kChecks));
    for (const CheckEntry& entry : kChecks) {
        names.emplace_back(entry.name);
    }
    return names;
}

int RunAll() {
    // Keep libav chatter off stderr — encoder stats are noise here;
    // real encode failures still surface through the check's reason.
    FFmpeg::SetLogLevel(FFmpeg::LogLevel::kError);

    // Hard cap: a hung check (deadlocked decoder, stuck V8 isolate)
    // still takes the process down non-zero instead of blocking a
    // harness forever. Cancellable: once the suite reports its result
    // the flag is armed so a slow CRT/FFmpeg teardown past the
    // deadline can no longer fire a spurious _Exit after
    // "SMOKE RESULT OK". Detached — normal exit kills it first.
    std::atomic<bool> watchdog_done{false};
    std::thread([&watchdog_done] {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(kWatchdogSeconds);
        while (std::chrono::steady_clock::now() < deadline &&
               !watchdog_done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }
        if (watchdog_done.load(std::memory_order_acquire)) {
            return;
        }
        std::fprintf(stdout,
                     "SMOKE watchdog FAIL exceeded %d seconds\n",
                     kWatchdogSeconds);
        std::fflush(stdout);
        std::_Exit(1);
    }).detach();

    int failures = 0;
    for (const CheckEntry& entry : kChecks) {
        const auto started = std::chrono::steady_clock::now();
        const std::string reason = entry.fn();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        if (reason.empty()) {
            std::fprintf(stdout, "SMOKE %s OK\n", entry.name);
        } else {
            ++failures;
            std::fprintf(stdout, "SMOKE %s FAIL %s\n", entry.name,
                         reason.c_str());
        }
        std::fprintf(stderr, "  (%lld ms)\n",
                     static_cast<long long>(elapsed));
        std::fflush(stdout);
    }
    std::fprintf(stdout, "SMOKE RESULT %s\n",
                 failures == 0 ? "OK" : "FAIL");
    std::fflush(stdout);
    // Disarm the watchdog before teardown — the result line is out.
    watchdog_done.store(true, std::memory_order_release);
    return failures == 0 ? 0 : 1;
}

}  // namespace XER::VideoEditor::Smoke
