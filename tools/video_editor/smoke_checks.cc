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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "audio_mixer.h"
#include "audio_sync.h"
#include "compositor.h"
#include "content/video/video_info.h"
#include "export_engine.h"
#include "export_preset.h"
#include "frame.h"
#include "frame_exporter.h"
#include "generated_source.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "js_fx.h"
#include "keyboard_map.h"
#include "media_catalog.h"
#include "media_worker_client.h"
#include "media_worker_protocol.h"
#include "poster_cache.h"
#include "project_autosave.h"
#include "project_store.h"
#include "sequence_preset.h"
#include "source_edit.h"
#include "timecode.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "worker_pipe.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#if defined(_WIN32)
#include <windows.h>   // GetModuleFileNameW — worker-exe sibling lookup
#endif

namespace XER::VideoEditor {
namespace {

// Canonical importable-media extension list — the single source of
// truth behind MediaImportExtensions(); see smoke_checks.h for why it
// lives in the UI-free core.
const std::string kMediaImportExtensions =
    // video containers FFmpeg demuxes
    "mp4;m4v;mkv;mov;qt;avi;divx;wmv;asf;webm;mpg;mpeg;mpe;m1v;m2v;mpv;"
    "ts;mts;m2ts;vob;3gp;3g2;flv;f4v;mxf;dv;dif;ogv;rm;rmvb;y4m;nut;bik;amv;"
    // audio-only containers
    "wav;mp3;m4a;m4b;aac;flac;ogg;oga;opus;wma;aif;aiff;aifc;mka;ac3;"
    "eac3;dts;amr;au;caf;wv;ape;tta;spx;voc;gsm;oma;"
    // still images (FFmpeg image2 + OIIO set)
    "png;jpg;jpeg;jpe;bmp;dib;gif;tif;tiff;webp;tga;ppm;pgm;pbm;pam;xpm;"
    "avif;heic;exr;dpx;hdr;psd;pfm;raw;cr2;nef;arw;dng;ora";

}  // namespace

const std::string& MediaImportExtensions() {
    return kMediaImportExtensions;
}

bool IsMediaImportPath(const std::string& utf8_path) {
    // Extension after the last '.', unless a path separator follows
    // it (a dot inside a directory name is not an extension).
    const std::size_t dot = utf8_path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= utf8_path.size()) {
        return false;
    }
    const std::size_t sep = utf8_path.find_last_of("/\\");
    if (sep != std::string::npos && sep > dot) return false;
    const std::string ext =
        XER::Helper::String::ToLowerAscii(utf8_path.substr(dot + 1));
    // Token-compare, never substring — "xmp4" must not match "mp4".
    std::size_t pos = 0;
    while (true) {
        const std::size_t next = kMediaImportExtensions.find(';', pos);
        const std::string_view token =
            std::string_view(kMediaImportExtensions)
                .substr(pos, next == std::string::npos
                                 ? std::string::npos
                                 : next - pos);
        if (token == ext) return true;
        if (next == std::string::npos) return false;
        pos = next + 1;
    }
}

namespace Smoke {
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

// Checked-in pixel baselines live in the SOURCE tree at
// tools/video_editor/tests/baselines — the exe sits under
// out/build/<preset>/bin/, so the dir is located by walking ancestors
// of the exe path (and the CWD as a fallback) until a directory that
// contains tools/video_editor/CMakeLists.txt turns up; that anchor
// keeps a stray same-named folder from fooling the search.
// XER_VE_BASELINE_DIR overrides discovery entirely. Empty = not found.
std::filesystem::path SmokeBaselineDir() {
    if (const char* env = std::getenv("XER_VE_BASELINE_DIR");
        env != nullptr && *env != '\0') {
        return Path::FromUtf8(env);
    }
    std::error_code ec;
    std::vector<std::filesystem::path> starts;
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        starts.push_back(std::filesystem::path(exe).parent_path());
    }
#elif defined(__linux__)
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) starts.push_back(self.parent_path());
#endif
    starts.push_back(std::filesystem::current_path(ec));
    for (const auto& start : starts) {
        std::filesystem::path dir = start;
        for (int up = 0; up < 8; ++up) {
            const auto ve = dir / "tools" / "video_editor";
            if (std::filesystem::exists(ve / "CMakeLists.txt", ec)) {
                return ve / "tests" / "baselines";
            }
            const auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
    }
    return {};
}

// PNG -> packed RGB8 via the FFmpeg image2 decode path — the same
// bridge FrameExporter wrote the file through, so encode/decode stay
// version-paired. Handles the packed layouts the PNG decoder emits
// (rgba/bgra/argb/abgr/rgb24/bgr24/gray); anything else (planar gbrp,
// 16-bit, palette) fails loudly so the diff never silently compares
// the wrong channels.
bool DecodePngToRgb8(const std::string& path,
                     std::vector<std::uint8_t>* rgb, int* width,
                     int* height, std::string* err) {
    std::vector<FFmpeg::VideoFrameInfo> frames;
    if (!FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, err) ||
        frames.empty()) {
        if (err != nullptr && err->empty()) *err = "no frame decoded";
        return false;
    }
    const FFmpeg::VideoFrameInfo& f = frames.front();
    const std::string& fmt = f.pixel_format;
    int ch = 0, r = 0, g = 0, b = 0;   // channels + byte offsets
    if (fmt == "rgba")       { ch = 4; r = 0; g = 1; b = 2; }
    else if (fmt == "bgra")  { ch = 4; r = 2; g = 1; b = 0; }
    else if (fmt == "argb")  { ch = 4; r = 1; g = 2; b = 3; }
    else if (fmt == "abgr")  { ch = 4; r = 3; g = 2; b = 1; }
    else if (fmt == "rgb24" || fmt == "rgb")
        { ch = 3; r = 0; g = 1; b = 2; }
    else if (fmt == "bgr24" || fmt == "bgr")
        { ch = 3; r = 2; g = 1; b = 0; }
    else if (fmt == "gray" || fmt == "gray8") {
        ch = 1;   // r/g/b all read offset 0
    } else {
        if (err != nullptr) {
            *err = "unsupported decoded pixel format '" + fmt + "'";
        }
        return false;
    }
    const std::size_t pixels =
        static_cast<std::size_t>(f.width) * f.height;
    if (f.width <= 0 || f.height <= 0 ||
        f.data.size() < pixels * static_cast<std::size_t>(ch)) {
        if (err != nullptr) *err = "decoded buffer short for " + fmt;
        return false;
    }
    rgb->resize(pixels * 3);
    const std::uint8_t* src = f.data.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        (*rgb)[3 * i + 0] = src[ch * i + r];
        (*rgb)[3 * i + 1] = src[ch * i + g];
        (*rgb)[3 * i + 2] = src[ch * i + b];
    }
    *width = f.width;
    *height = f.height;
    return true;
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

// The `--screenshot` pipeline in-process: a project round-trips
// through ProjectStore, the program frame renders through the
// headless Compositor and lands on disk as a PNG via
// video::FrameExporter — the file must carry the PNG magic plus an
// IHDR matching the canvas size.
std::string CheckScreenshotPng() {
    Project project = MakeRenderProject(160, 90, 15);
    const std::string proj_path =
        Utf8Path(SmokeTempDir() / "shot_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, proj_path, &err)) {
        return "project save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(proj_path, &loaded, &err)) {
        return "project reload failed: " + err;
    }
    Compositor compositor;
    if (!compositor.Init(loaded, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    video::Frame frame;
    if (!frame.Allocate(160, 90, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    std::memcpy(frame.Data(), bgra.data(), bgra.size());
    const std::string png_path =
        Utf8Path(SmokeTempDir() / "shot_project.png");
    video::FrameExporter exporter;
    if (!exporter.ToFile(png_path, frame)) {
        return "PNG export failed";
    }
    std::ifstream in(Path::FromUtf8(png_path),
                     std::ios::binary | std::ios::ate);
    if (!in) {
        return "PNG missing after export";
    }
    const auto size = in.tellg();
    if (size < 64) {   // header alone is 33 bytes; bars need data too
        return "PNG suspiciously small (" +
               std::to_string(static_cast<long long>(size)) + " bytes)";
    }
    in.seekg(0);
    std::uint8_t header[24] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    static const std::uint8_t kMagic[8] = {0x89, 'P', 'N', 'G',
                                           0x0D, 0x0A, 0x1A, 0x0A};
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header)) ||
        std::memcmp(header, kMagic, 8) != 0) {
        return "PNG magic mismatch";
    }
    // Bytes 8..11 = IHDR length (13 BE), 12..15 = "IHDR",
    // 16..23 = width/height big-endian.
    const auto be32 = [](const std::uint8_t* p) {
        return (static_cast<std::uint32_t>(p[0]) << 24) |
               (static_cast<std::uint32_t>(p[1]) << 16) |
               (static_cast<std::uint32_t>(p[2]) << 8) |
               static_cast<std::uint32_t>(p[3]);
    };
    if (be32(header + 8) != 13 ||
        std::memcmp(header + 12, "IHDR", 4) != 0 ||
        be32(header + 16) != 160 || be32(header + 20) != 90) {
        return "IHDR dimensions mismatch";
    }
    return {};
}

// `ui_screenshot_diff` — pixel-diff regression for the --screenshot
// pipeline. SCOPE NOTE (determinism): the diff covers the program-
// frame path only — ProjectStore -> Compositor -> FrameExporter PNG
// -> FFmpeg decode -> per-pixel compare. The full `--ui` chrome
// capture is deliberately out of scope: it lives exe-side (the
// core lib cannot link XER_UI_Core) and rasterizes fonts, panels and
// toasts whose output varies with the host's text stack — far too
// machine-dependent for a checked-in baseline. The program frame is
// fully procedural (SMPTE-75 generated bars on a fixed 1280x720
// canvas at t = 0.5, mid-clip), so it renders identically every run;
// the tolerance below absorbs codec/rasterizer-version drift only —
// content changes always exceed it.
//
// Baseline: tools/video_editor/tests/baselines/ui_default.png
// (located via SmokeBaselineDir; XER_VE_BASELINE_DIR overrides).
// A missing baseline is WRITTEN and the check passes — the first
// run establishes it; XER_VE_UPDATE_BASELINE=1 force-refreshes.
std::string CheckUiScreenshotDiff() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const std::filesystem::path baseline_dir = SmokeBaselineDir();
    if (baseline_dir.empty()) {
        return "tools/video_editor/tests/baselines not located "
               "(set XER_VE_BASELINE_DIR)";
    }
    const std::filesystem::path baseline =
        baseline_dir / "ui_default.png";

    // Same steps as RunScreenshotProgram in main.cc: save -> load ->
    // compositor -> RenderFrameAt -> PNG via FrameExporter.
    Project project = MakeRenderProject(1280, 720, 30);
    const std::string proj_path =
        Utf8Path(SmokeTempDir() / "diff_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, proj_path, &err)) {
        return "project save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(proj_path, &loaded, &err)) {
        return "project reload failed: " + err;
    }
    Compositor compositor;
    if (!compositor.Init(loaded, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    const bool any_pixels =
        std::any_of(bgra.begin(), bgra.end(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "rendered frame is all zeros";
    }
    video::Frame frame;
    if (!frame.Allocate(1280, 720, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    std::memcpy(frame.Data(), bgra.data(), bgra.size());
    const std::string shot_path =
        Utf8Path(SmokeTempDir() / "ui_default_actual.png");
    video::FrameExporter exporter;
    if (!exporter.ToFile(shot_path, frame)) {
        return "PNG export failed";
    }

    std::error_code ec;
    const bool have_baseline =
        std::filesystem::exists(baseline, ec) &&
        std::filesystem::file_size(baseline, ec) > 0;
    const char* update_env = std::getenv("XER_VE_UPDATE_BASELINE");
    const bool force_update =
        update_env != nullptr &&
        (std::string_view(update_env) == "1" ||
         std::string_view(update_env) == "true");
    if (!have_baseline || force_update) {
        std::filesystem::create_directories(baseline_dir, ec);
        if (!std::filesystem::copy_file(
                Path::FromUtf8(shot_path), baseline,
                std::filesystem::copy_options::overwrite_existing,
                ec)) {
            return "baseline write failed: " + ec.message();
        }
        std::fprintf(stderr,
                     "  ui_screenshot_diff: baseline %s -> %s\n",
                     force_update ? "refreshed" : "written",
                     Utf8Path(baseline).c_str());
        return {};
    }

    std::vector<std::uint8_t> actual_rgb, base_rgb;
    int aw = 0, ah = 0, bw = 0, bh = 0;
    if (!DecodePngToRgb8(shot_path, &actual_rgb, &aw, &ah, &err)) {
        return "actual decode failed: " + err;
    }
    if (!DecodePngToRgb8(Utf8Path(baseline), &base_rgb, &bw, &bh,
                         &err)) {
        return "baseline decode failed: " + err;
    }
    if (aw != bw || ah != bh) {
        return "size mismatch: actual " + std::to_string(aw) + "x" +
               std::to_string(ah) + " vs baseline " +
               std::to_string(bw) + "x" + std::to_string(bh);
    }
    // Tolerance: mean |delta| < 4.0 per channel-byte AND <1% of
    // pixels differ by >16 on any channel. PNG is lossless, so a
    // healthy run diffs at ~0; the slack only absorbs FFmpeg-version
    // pixel-format/dither drift, never content changes.
    std::uint64_t sum = 0;
    std::size_t hot = 0;
    const std::size_t pixels = static_cast<std::size_t>(aw) * ah;
    for (std::size_t i = 0; i < pixels; ++i) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(
                static_cast<int>(actual_rgb[3 * i + c]) -
                static_cast<int>(base_rgb[3 * i + c]));
            worst = std::max(worst, d);
            sum += static_cast<std::uint64_t>(d);
        }
        if (worst > 16) ++hot;
    }
    const double mean =
        static_cast<double>(sum) / static_cast<double>(pixels * 3);
    const double hot_frac = static_cast<double>(hot) / pixels;
    if (mean >= 4.0 || hot_frac >= 0.01) {
        char stats[96];
        std::snprintf(stats, sizeof(stats),
                      "mean=%.3f hot=%.2f%%", mean, hot_frac * 100.0);
        return std::string("diff too large: ") + stats +
               " (baseline " + Utf8Path(baseline) +
               "; set XER_VE_UPDATE_BASELINE=1 to refresh)";
    }
    return {};
}


// Curve easings + glass/lens kernels: new KeyEase curves must land on
// their endpoints (Bounce/Elastic/Back overshoot mid-span only); the
// refraction/DOF kernels must visibly move pixels on a bars frame.
std::string CheckCurvesAndLens() {
    Clip clip;
    clip.id = "curve";
    clip.duration = 4.0;
    clip.keyframes[KeyProp::Scale] = {
        {0.0, 1.0f, KeyEase::Bounce},
        {2.0, 2.0f, KeyEase::Linear},
        {4.0, 1.0f, KeyEase::Linear},
    };
    // Endpoints are exact regardless of the curve shape.
    if (std::fabs(EvaluateClipProperty(clip, KeyProp::Scale, 0.0, 1.0f) -
                  1.0f) > 1e-5f ||
        std::fabs(EvaluateClipProperty(clip, KeyProp::Scale, 2.0, 1.0f) -
                  2.0f) > 1e-5f) {
        return "eased curve missed its endpoints";
    }
    // Bounce mid-segment must sit between the key values.
    const float mid =
        EvaluateClipProperty(clip, KeyProp::Scale, 1.0, 1.0f);
    if (mid < 1.0f || mid > 2.0f) {
        return "bounce curve left the key range mid-segment";
    }
    for (KeyEase e : {KeyEase::Back, KeyEase::Elastic, KeyEase::ExpoOut,
                      KeyEase::EaseInCubic, KeyEase::EaseOutCubic}) {
        clip.keyframes[KeyProp::Scale].front().easing = e;
        const float end =
            EvaluateClipProperty(clip, KeyProp::Scale, 2.0 - 1e-9, 1.0f);
        if (std::fabs(end - 2.0f) > 0.05f) {
            return "easing does not settle on the segment end";
        }
    }

    Project project = MakeRenderProject(160, 90, 15);
    Clip* bars = &project.clips[0];
    bars->effects = {
        {"lens_sphere", {0.8f, 1.5f, 2.0f, 0.3f, 0.5f}},
        {"ribbed_glass", {16.0f, 3.0f, 0.0f, 0.3f}},
        {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.1f, 0.4f, 8.0f, 2.0f}},
    };
    Compositor compositor;
    std::string err;
    if (!compositor.Init(project, {}, &err)) {
        return "lens compositor init failed: " + err;
    }
    std::vector<std::uint8_t> fx_bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &fx_bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "lens frame render failed: " + err;
    }

    project.clips[0].effects.clear();
    Compositor plain;
    if (!plain.Init(project, {}, &err)) {
        return "plain compositor init failed: " + err;
    }
    std::vector<std::uint8_t> plain_bgra;
    const bool plain_ok = plain.RenderFrameAt(0.5, &plain_bgra, &err);
    plain.Shutdown();
    if (!plain_ok || plain_bgra.size() != fx_bgra.size()) {
        return "reference frame failed";
    }
    std::size_t moved = 0;
    for (std::size_t i = 0; i < fx_bgra.size(); i += 4) {
        if (fx_bgra[i] != plain_bgra[i] ||
            fx_bgra[i + 1] != plain_bgra[i + 1] ||
            fx_bgra[i + 2] != plain_bgra[i + 2]) {
            ++moved;
        }
    }
    if (moved < fx_bgra.size() / 4 / 10) {
        return "lens effects moved too few pixels";
    }
    return {};
}

// JS fx: the catalog must list our stock scripts, and — when V8 is
// compiled in — a runtime must register + execute them. invert on a
// black pixel yields 255; bitcrush quantizes; smoothstep(0.5) = 0.5.
std::string CheckJsFx() {
    std::string status;
    AppRuntime::Boot(&status);   // fills SharedAssetsConfig
    const std::string fx_dir =
        (AppRuntime::SharedAssetsConfig().assets_dir /
         "scripts" / "fx").string();
    std::string err;
    if (!JsFxRuntime::LoadCatalog(fx_dir, &err)) {
        return "fx catalog load failed: " + err;
    }
    if (JsFxRuntime::VideoEffects().size() < 5 ||
        JsFxRuntime::AudioEffects().size() < 4 ||
        JsFxRuntime::ScriptTransitions().size() < 5) {
        return "fx catalog lists too few entries";
    }
    JsFxRuntime fx;
    if (!fx.EnsureLoaded(&err)) {
#if defined(XER_VE_HAS_V8)
        return "js fx runtime failed: " + err;
#else
        return {};   // no-V8 build — catalog-only check passes
#endif
    }
    if (!fx.HasVideoEffect("invert") ||
        !fx.HasAudioEffect("bitcrush") ||
        !fx.HasTransition("smoothstep")) {
        return "registered ids missing from the runtime";
    }
    video::Frame frame;
    if (!frame.Allocate(4, 4, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    if (!fx.ApplyVideo("invert", &frame, {}, 0.0, "bars", &err)) {
        return "invert apply failed: " + err;
    }
    if (frame.Data()[0] != 255 || frame.Data()[1] != 255 ||
        frame.Data()[2] != 255) {
        return "invert produced wrong pixels";
    }
    float pcm[4] = {0.1f, -0.1f, 0.9f, -0.9f};
    if (!fx.ProcessAudio("bitcrush", pcm, 2, 48000, {{0, 4.0f}}, 0.0,
                         "bars", &err)) {
        return "bitcrush process failed: " + err;
    }
    // 4-bit → 16 levels; 0.9 quantizes to a multiple of 1/16.
    const float q = pcm[2] * 16.0f;
    if (std::fabs(q - std::round(q)) > 0.01f) {
        return "bitcrush output not quantized";
    }
    const double w = fx.TransitionWeight("smoothstep", 0.5);
    if (std::fabs(w - 0.5) > 0.01) {
        return "smoothstep(0.5) != 0.5";
    }

    // ---- hardening: hostile scripts ----------------------------------
    // A private catalog + runtime exercises the failure paths the
    // stock scripts never hit: a throwing apply(), a runaway loop the
    // watchdog must terminate, and a stateful counter proving
    // ctx.clip_id scopes closure state per clip.
    const auto hostile_dir = SmokeTempDir() / "fx_hostile";
    {
        std::error_code ec;
        std::filesystem::create_directories(hostile_dir, ec);
        auto write = [&](const char* name, const char* text) {
            std::ofstream(hostile_dir / name,
                          std::ios::binary | std::ios::trunc)
                << text;
        };
        write("fx.json",
              "{\"video\":["
              "{\"id\":\"thrower\",\"file\":\"thrower.js\",\"params\":[]},"
              "{\"id\":\"runaway\",\"file\":\"runaway.js\",\"params\":[]},"
              "{\"id\":\"ok\",\"file\":\"ok.js\",\"params\":[]}],"
              "\"audio\":["
              "{\"id\":\"counter\",\"file\":\"counter.js\",\"params\":[]}],"
              "\"transitions\":[]}");
        write("thrower.js",
              "xerFxVideo({id:\"thrower\",apply(f,p,c){"
              "throw new Error(\"smoke boom\");}});\n");
        write("runaway.js",
              "xerFxVideo({id:\"runaway\",apply(f,p,c){"
              "while(true){}}});\n");
        write("ok.js",
              "xerFxVideo({id:\"ok\",apply(f,p,c){"
              "f.data[0]=42;}});\n");
        write("counter.js",
              "xerFxAudio(function(){const counts={};return{"
              "id:\"counter\",process(pcm,p,ctx){"
              "const k=ctx.clip_id||\"\";"
              "const c=(counts[k]=(counts[k]||0)+1);"
              "for(let i=0;i<pcm.length;++i)pcm[i]=c;}};}());\n");
    }
    if (!JsFxRuntime::LoadCatalog(hostile_dir.string(), &err)) {
        return "hostile fx catalog failed: " + err;
    }
    JsFxRuntime hostile;
    if (!hostile.EnsureLoaded(&err)) {
        return "hostile js runtime failed: " + err;
    }
    // (a) exception inside apply(): false + error text, no crash.
    video::Frame f2;
    if (!f2.Allocate(2, 2, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    if (hostile.ApplyVideo("thrower", &f2, {}, 0.0, "c1", &err)) {
        return "throwing apply() reported success";
    }
    if (err.find("boom") == std::string::npos) {
        return "apply() error text lost: " + err;
    }
    // (b) runaway apply(): the watchdog (500 ms budget) must kill it —
    // generous 4 s ceiling for CI jitter. Returns false (identity).
    const auto runaway_t0 = std::chrono::steady_clock::now();
    const bool runaway_ok =
        hostile.ApplyVideo("runaway", &f2, {}, 0.0, "c1", &err);
    const auto runaway_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - runaway_t0)
            .count();
    if (runaway_ok) {
        return "runaway apply() reported success";
    }
    if (runaway_ms > 4000) {
        return "runaway script not terminated (" +
               std::to_string(static_cast<long long>(runaway_ms)) +
               "ms)";
    }
    // The isolate must stay usable — termination is per-call.
    if (!hostile.ApplyVideo("ok", &f2, {}, 0.0, "c1", &err)) {
        return "runtime stayed broken after termination: " + err;
    }
    if (f2.Data()[0] != 42) {
        return "ok script did not run after termination";
    }
    // (c) ctx.clip_id scopes stateful audio closures: clipA counts
    // 1,2 while clipB starts its own counter at 1.
    float pcm_c[2] = {0.0f, 0.0f};
    auto count_for = [&](const std::string& clip) {
        pcm_c[0] = 0.0f;
        if (!hostile.ProcessAudio("counter", pcm_c, 1, 48000, {}, 0.0,
                                  clip, &err)) {
            return -1.0f;
        }
        return pcm_c[0];
    };
    if (count_for("clipA") != 1.0f || count_for("clipA") != 2.0f ||
        count_for("clipB") != 1.0f) {
        return "ctx.clip_id did not isolate per-clip script state";
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

// Synthetic waveform peaks shared by the audio-sync checks: a
// deterministic non-periodic energy pattern shifted by `shift`
// buckets (positive = the audio lands later in this file's own
// recording), noise floor off-pattern.
AudioPeaks MakeSyncTestPeaks(int buckets, int shift) {
    const auto pattern = [](int i) {
        std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u;
        h ^= h >> 16;
        return 0.05f + 0.9f * (h % 1024) / 1024.0f;
    };
    AudioPeaks peaks;
    peaks.channels = 1;
    peaks.samples_per_peak = 256;
    peaks.buckets = static_cast<std::size_t>(buckets);
    peaks.data.resize(static_cast<std::size_t>(buckets) * 2);
    for (int b = 0; b < buckets; ++b) {
        const int src = b - shift;
        const float v = (src >= 0 && src < buckets)
                            ? pattern(src)
                            : 0.02f;  // noise floor off-pattern
        peaks.data[2 * b] = -v;
        peaks.data[2 * b + 1] = v;
    }
    return peaks;
}

// Audio-sync lag math (audio_sync.h): synthetic AudioPeaks driven
// through the peaks-provider seam — no decoder, no media files. B is
// A delayed by kShift buckets: the plan must recover that lag and
// emit a NEGATIVE timeline shift of kShift bucket-seconds (the clip
// moves earlier because its take's audio sits later in its own
// recording). A leading copy proves the positive direction, and a
// noise-only clip proves the confidence gate.
std::string CheckAudioSync() {
    constexpr int kBuckets = 4096;   // ~21.8 s at 256/48k per bucket
    constexpr int kShift = 148;      // buckets; 37 envelope cells (x4)
    constexpr double kCell = 256.0 / 48000.0 * 4.0;  // env cell secs
    // Peak buckets for the reference pattern shifted by `shift`
    // (positive = audio lands later in this file's own recording).
    const auto make_peaks = [](int shift) {
        return MakeSyncTestPeaks(kBuckets, shift);
    };
    const AudioPeaks peaks_a = make_peaks(0);
    const AudioPeaks peaks_b = make_peaks(kShift);    // delayed copy
    const AudioPeaks peaks_d = make_peaks(-kShift);   // leading copy
    const AudioPeaks peaks_n = make_peaks(1 << 30);   // pure floor

    const auto provider = [&](const std::string& path,
                              AudioPeaks* out) -> bool {
        if (path == "cam_a.mp4") { *out = peaks_a; return true; }
        if (path == "cam_b.mp4") { *out = peaks_b; return true; }
        if (path == "cam_d.mp4") { *out = peaks_d; return true; }
        if (path == "cam_n.mp4") { *out = peaks_n; return true; }
        return false;
    };

    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A2"});
    project.tracks.push_back(Track{TrackKind::Audio, "A3"});
    const auto media_clip = [](const std::string& id,
                               const std::string& path, int track,
                               double start, double dur) {
        Clip clip;
        clip.id = id;
        clip.source_path = path;
        clip.source.kind = SourceKind::Media;
        clip.track = track;
        clip.timeline_start = start;
        clip.duration = dur;
        return clip;
    };
    project.clips.push_back(
        media_clip("ref", "cam_a.mp4", 0, 0.0, 20.0));
    project.clips.push_back(
        media_clip("delayed", "cam_b.mp4", 1, 5.0, 20.0));
    project.clips.push_back(
        media_clip("leading", "cam_d.mp4", 2, 2.0, 20.0));
    project.clips.push_back(
        media_clip("noise", "cam_n.mp4", 3, 12.0, 10.0));

    AudioSyncReport report;
    std::string err;
    if (!ComputeAudioSyncPlanWithPeaks(project, "ref", provider,
                                       &report, &err)) {
        return "plan failed: " + err;
    }
    if (report.reference_clip_id != "ref" || report.status.empty()) {
        return "report missing reference/status";
    }
    const auto entry_for = [&](const std::string& id)
        -> const AudioSyncEntry* {
        for (const auto& e : report.entries) {
            if (e.clip_id == id) return &e;
        }
        return nullptr;
    };
    // lag cells = kShift/4 = 37 → delta = -37 * kCell ≈ -0.789 s.
    const double want_delta = -(kShift / 4.0) * kCell;
    const AudioSyncEntry* delayed = entry_for("delayed");
    if (delayed == nullptr || !delayed->matched) {
        return "delayed clip did not match";
    }
    if (std::fabs(delayed->delta_seconds - want_delta) > 1e-3 ||
        std::fabs(delayed->new_timeline_start -
                  (5.0 + want_delta)) > 1e-3) {
        return "delayed clip shift wrong: " +
               std::to_string(delayed->delta_seconds);
    }
    if (delayed->confidence < 0.5) {
        return "delayed clip confidence too low";
    }
    const AudioSyncEntry* leading = entry_for("leading");
    if (leading == nullptr || !leading->matched) {
        return "leading clip did not match";
    }
    if (std::fabs(leading->delta_seconds + want_delta) > 1e-3 ||
        std::fabs(leading->new_timeline_start -
                  (2.0 - want_delta)) > 1e-3) {
        return "leading clip shift wrong (sign): " +
               std::to_string(leading->delta_seconds);
    }
    const AudioSyncEntry* noise = entry_for("noise");
    if (noise == nullptr || noise->matched) {
        return "noise clip falsely matched";
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

// IsAttachedPictureStream() — the exported seam ProbeVideo applies to
// every "video"-typed stream to decide whether it is embedded cover
// art (never the primary video) or real footage. Truth-table the real
// contract: attached_pic disposition, "cover" metadata, any picture
// stream in an audio-primary container, and a 1-2 frame image-codec
// stream beside audio all classify as art — while MJPEG camera
// footage with an unknown frame count and lone stills must stay video.
std::string CheckProbeClassification() {
    namespace CV = XER::Content::Video;
    const std::string mp4 = "mov,mp4,m4a,3gp,3g2,mj2";

    FFmpeg::StreamInfo base;
    base.media_type = "video";
    base.codec_name = "mjpeg";

    // attached_pic disposition beside audio -> art.
    FFmpeg::StreamInfo pic = base;
    pic.attached_pic = true;
    pic.frame_count = 1;
    if (!CV::IsAttachedPictureStream(pic, true, mp4)) {
        return "attached_pic stream not classified as art";
    }
    // "cover" metadata marker -> art even without an audio stream
    // (the metadata check runs before the audio gate).
    FFmpeg::StreamInfo meta = base;
    meta.metadata.push_back({"comment", "Cover (front)"});
    if (!CV::IsAttachedPictureStream(meta, false, mp4)) {
        return "cover metadata not classified as art";
    }
    // mjpeg inside an audio-primary container (mp3) with 1-2 frames
    // next to audio -> art even with no disposition/metadata tells.
    FFmpeg::StreamInfo mp3_art = base;
    for (const std::int64_t frames : {1, 2}) {
        mp3_art.frame_count = frames;
        if (!CV::IsAttachedPictureStream(mp3_art, true, "mp3")) {
            return "mp3 picture stream (" + std::to_string(frames) +
                   " frames) not classified as art";
        }
    }
    // MJPEG camera footage: image codec but frame_count unknown (0)
    // in a general container -> stays video.
    FFmpeg::StreamInfo camera = base;  // frame_count stays 0
    if (CV::IsAttachedPictureStream(camera, true, mp4)) {
        return "uncounted MJPEG footage misclassified as art";
    }
    // Many image-codec frames -> a real video, not a cover.
    camera.frame_count = 300;
    if (CV::IsAttachedPictureStream(camera, true, mp4)) {
        return "300-frame MJPEG stream misclassified as art";
    }
    // A 1-2 frame image-codec stream beside audio in a general
    // container -> art by the frame-count heuristic.
    FFmpeg::StreamInfo still = base;
    still.codec_name = "png";
    still.frame_count = 1;
    if (!CV::IsAttachedPictureStream(still, true, mp4)) {
        return "1-frame image stream beside audio not classified as art";
    }
    // The same lone still with no audio anywhere keeps counting as
    // video — the audio gate keeps single pictures usable.
    if (CV::IsAttachedPictureStream(still, false, mp4)) {
        return "lone still without audio misclassified as art";
    }
    // A real video codec never classifies on frame count alone.
    FFmpeg::StreamInfo h264 = base;
    h264.codec_name = "h264";
    h264.frame_count = 1;
    if (CV::IsAttachedPictureStream(h264, true, mp4)) {
        return "1-frame h264 stream misclassified as art";
    }
    return {};
}

// PosterCache (poster_cache.h) byte-budgeted LRU: an oversized frame
// is never retained, many small frames evict LRU-first until
// BytesUsed() fits the capacity, and a Get() hit promotes the key so
// it survives further evictions. UI-free — frames only, no decoder.
std::string CheckPosterCacheLru() {
    constexpr std::size_t kCapacity = 1u * 1024u * 1024u;  // 1 MiB
    PosterCache cache(kCapacity);

    // Oversized: a 2 MiB frame exceeds the whole budget -> the cache
    // drops it immediately instead of keeping it.
    video::Frame big;
    if (!big.Allocate(512, 1024, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    cache.Put("big.mp4", 0, std::move(big));
    if (cache.Get("big.mp4", 0) != nullptr) {
        return "oversized frame was retained";
    }

    // 64 KiB frames: 16 fit exactly in 1 MiB. One hot key plus 15 cold
    // keys, touch the hot one, then churn 8 more — the promoted key
    // must survive while the oldest cold keys evict.
    video::Frame small;
    if (!small.Allocate(128, 128, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    cache.Put("hot.mp4", 0, small);
    for (int i = 0; i < 15; ++i) {
        cache.Put("clip.mp4", i, small);
    }
    if (cache.Get("hot.mp4", 0) == nullptr) {
        return "hot key missing before the churn";
    }
    for (int i = 0; i < 8; ++i) {
        cache.Put("filler.mp4", i, small);
    }
    const PosterCache::Stats stats = cache.GetStats();
    if (stats.bytes > stats.capacity_bytes) {
        return "BytesUsed() exceeds capacity";
    }
    if (stats.evictions == 0) {
        return "no evictions recorded under pressure";
    }
    if (cache.Get("hot.mp4", 0) == nullptr) {
        return "hot key evicted despite Get() promotion";
    }
    return {};
}

// Deployed assets audit: the app boots off <exe>/assets/app.yaml, the
// icon pack rasterizes SVGs resolved through icons.yaml, and the
// .xexrj extension host scans assets/extensions/. Everything the
// runtime loads must actually be deployed — missing entries are
// reported in the error string.
std::string CheckAssetsIcons() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);  // resolves assets_dir
    // Same hygiene as app_boot: drop the published engine pointer
    // before this result dies.
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return "assets dir not deployed (app.yaml not found)";
    }
    std::error_code ec;
    std::vector<std::string> missing;
    if (!std::filesystem::exists(cfg.assets_dir / "app.yaml", ec)) {
        missing.push_back("app.yaml");
    }
    if (!std::filesystem::exists(cfg.assets_dir / "icons.yaml", ec)) {
        missing.push_back("icons.yaml");
    }
    // Icons dir: the path Boot resolved, falling back to the
    // conventional assets/icons layout.
    const std::filesystem::path icons_dir =
        !cfg.icons_dir.empty() ? cfg.icons_dir
                               : cfg.assets_dir / "icons";
    if (!std::filesystem::is_directory(icons_dir, ec)) {
        missing.push_back("icons/");
    } else {
        int svg_count = 0;
        for (const auto& de :
             std::filesystem::directory_iterator(icons_dir, ec)) {
            if (de.path().extension() == ".svg") {
                ++svg_count;
            }
        }
        if (svg_count == 0) {
            missing.push_back("icons/*.svg");
        }
        // Every icons.yaml mapping must resolve under the icons dir —
        // IconPack::Get() builds its path exactly this way.
        for (const auto& [name, file] : cfg.icon_map) {
            if (!std::filesystem::exists(icons_dir / file, ec)) {
                missing.push_back("icon:" + name + "->" + file);
            }
        }
    }
    // .xexrj extension packages — the ext host rescans this dir.
    const std::filesystem::path ext_dir = cfg.assets_dir / "extensions";
    bool has_xexrj = false;
    if (std::filesystem::is_directory(ext_dir, ec)) {
        for (const auto& de :
             std::filesystem::directory_iterator(ext_dir, ec)) {
            if (de.path().extension() == ".xexrj") {
                has_xexrj = true;
                break;
            }
        }
    }
    if (!has_xexrj) {
        missing.push_back("extensions/*.xexrj");
    }
    if (!missing.empty()) {
        std::string reason = "missing:";
        for (const std::string& entry : missing) {
            reason += " " + entry;
        }
        return reason;
    }
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

// Out-of-process media worker (media_worker_client.h): the client
// spawns the dedicated XERVideoLoader.exe sitting NEXT TO this exe
// when present — dev builds without it fall back to self-spawning
// `--media-worker`. Either way the worker must answer a ping; the
// dedicated case additionally checks the role string to prove the
// named binary (not the fallback, not a PATH lookalike) replied.
std::string CheckMediaWorker() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    const bool dedicated =
        std::filesystem::exists(bin_dir / "XERVideoLoader.exe", ec);
    auto& client = MediaWorkerClient::Shared();
    if (!client.Available()) {
        return "media worker spawn failed";
    }
    std::string role;
    if (!client.Ping(&role)) {
        return "media worker ping failed";
    }
    if (dedicated && role != "XERVideoLoader") {
        return "XERVideoLoader.exe present but worker role is '" +
               role + "'";
    }
    if (!dedicated && role != "media-worker") {
        return "unexpected worker role '" + role + "'";
    }
    return {};
#else
    return {};   // the subprocess client is Windows-only
#endif
}

// XERSyncMedia.exe (worker_pipe.h): the audio-sync plan computed
// INSIDE the named worker — the same synthetic-peaks math as the
// audio_sync check, but the peaks ride the sync_plan request's
// injection seam so no decoder runs. Asserts the dedicated exe's
// role string; when the exe is absent the documented in-process
// fallback is exercised instead.
std::string CheckSyncMediaWorker() {
#if defined(_WIN32)
    constexpr int kBuckets = 4096;
    constexpr int kShift = 148;
    constexpr double kCell = 256.0 / 48000.0 * 4.0;
    const AudioPeaks peaks_a = MakeSyncTestPeaks(kBuckets, 0);
    const AudioPeaks peaks_b = MakeSyncTestPeaks(kBuckets, kShift);

    // The minimal in-process sanity proof used when the worker exe is
    // not beside this binary (dev shells, partial builds).
    auto in_process = [&]() -> std::string {
        Project project;
        project.tracks.push_back(Track{TrackKind::Audio, "A1"});
        const auto media_clip = [](const std::string& id,
                                   const std::string& path,
                                   double start) {
            Clip clip;
            clip.id = id;
            clip.source_path = path;
            clip.source.kind = SourceKind::Media;
            clip.track = 0;
            clip.timeline_start = start;
            clip.duration = 20.0;
            return clip;
        };
        project.clips.push_back(media_clip("ref", "cam_a.mp4", 0.0));
        project.clips.push_back(
            media_clip("delayed", "cam_b.mp4", 5.0));
        const auto provider = [&](const std::string& path,
                                  AudioPeaks* out) -> bool {
            *out = path == "cam_b.mp4" ? peaks_b : peaks_a;
            return true;
        };
        AudioSyncReport report;
        std::string err;
        if (!ComputeAudioSyncPlanWithPeaks(project, "ref", provider,
                                           &report, &err)) {
            return "in-process fallback plan failed: " + err;
        }
        return {};
    };

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const bool dedicated = std::filesystem::exists(
        std::filesystem::path(exe).parent_path() /
        "XERSyncMedia.exe");
    WorkerPipe& pipe = SyncMediaPipe();
    if (!dedicated || !pipe.Available()) {
        return in_process();   // fallback tolerated
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "sync worker ping failed";
    }
    if (role != "XERSyncMedia") {
        return "unexpected sync worker role '" + role + "'";
    }

    MediaWorker::SyncPlanRequestData data;
    data.ref_index = 0;
    const auto clip = [](const char* id, const char* path,
                         double start, const AudioPeaks& peaks) {
        MediaWorker::SyncPlanClip c;
        c.clip_id = id;
        c.path = path;
        c.start = start;
        c.out_point = 20.0;
        c.rate = 1.0;
        c.has_peaks = true;
        c.peaks = peaks;
        return c;
    };
    data.clips.push_back(clip("ref", "cam_a.mp4", 0.0, peaks_a));
    data.clips.push_back(
        clip("delayed", "cam_b.mp4", 5.0, peaks_b));
    const std::uint64_t id = pipe.NextId();
    std::string reply;
    if (!pipe.RoundTrip(
            id, MediaWorker::EncodeSyncPlanRequest(id, data), 60000,
            &reply)) {
        return "sync_plan request got no reply";
    }
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!MediaWorker::DecodeReplyEnvelope(reply, &rid, &ok, &err)) {
        return "sync_plan reply is malformed";
    }
    if (!ok) {
        return "sync_plan rejected: " + err;
    }
    MediaWorker::SyncPlanResult result;
    if (!MediaWorker::DecodeSyncPlanReply(reply, &rid, &result)) {
        return "sync_plan reply payload failed to decode";
    }
    if (result.reference_clip_id != "ref") {
        return "sync_plan picked the wrong reference";
    }
    const MediaWorker::SyncPlanResultEntry* delayed = nullptr;
    for (const auto& e : result.entries) {
        if (e.clip_id == "delayed") delayed = &e;
    }
    if (delayed == nullptr || !delayed->matched) {
        return "worker did not match the delayed clip";
    }
    // lag cells = kShift/4 = 37 → delta = -37 * kCell ≈ -0.789 s.
    const double want_delta = -(kShift / 4.0) * kCell;
    if (std::fabs(delayed->delta_seconds - want_delta) > 1e-3) {
        return "worker shift has the wrong sign/magnitude: " +
               std::to_string(delayed->delta_seconds);
    }
    if (delayed->confidence < 0.5) {
        return "worker confidence too low";
    }
    return {};
#else
    return {};
#endif
}

// XERFxScript.exe (worker_pipe.h): the JS fx host answers over the
// pipe — fx_list must mirror the deployed catalog, fx_audio must
// actually run a script (bitcrush on a sine block quantizes), and
// fx_transition must return the smoothstep scalar. Asserts the role;
// a missing exe or a dead engine falls back to pass — js_fx already
// proves the in-process runtime.
std::string CheckFxScriptWorker() {
#if defined(_WIN32)
    std::string status;
    AppRuntime::Boot(&status);   // fills SharedAssetsConfig
    const std::string fx_dir =
        (AppRuntime::SharedAssetsConfig().assets_dir / "scripts" /
         "fx")
            .string();
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const bool dedicated = std::filesystem::exists(
        std::filesystem::path(exe).parent_path() / "XERFxScript.exe");
    WorkerPipe& pipe = FxScriptPipe();
    if (!dedicated || !pipe.Available()) {
        return {};   // fallback tolerated
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "fx worker ping failed";
    }
    if (role != "XERFxScript") {
        return "unexpected fx worker role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<std::string(std::uint64_t)>& encode,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            return pipe.RoundTrip(id, encode(id), 30000, reply);
        };
    // Tolerate engine-side rejections (V8-less worker, undeployed
    // catalog) but not transport silence — the worker answered ping.
    const auto envelope_ok = [](const std::string& reply,
                                std::string* err) -> int {
        std::uint64_t id = 0;
        bool ok = false;
        if (!MediaWorker::DecodeReplyEnvelope(reply, &id, &ok,
                                              err)) {
            return -1;   // malformed
        }
        return ok ? 1 : 0;
    };

    // fx_list — pure JSON; a live worker must always answer it.
    std::string reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxListRequest(id, fx_dir);
            },
            &reply)) {
        return "fx_list got no reply";
    }
    MediaWorker::FxListReply list;
    std::uint64_t rid = 0;
    if (!MediaWorker::DecodeFxListReply(reply, &rid, &list)) {
        return "fx_list reply failed to decode";
    }
    const auto has_id = [](const std::vector<MediaWorker::FxListItem>&
                               items,
                           const char* id) {
        for (const auto& item : items) {
            if (item.id == id) return true;
        }
        return false;
    };
    if (list.video.size() < 5 || list.audio.size() < 4 ||
        list.transitions.size() < 5 ||
        !has_id(list.video, "invert") ||
        !has_id(list.audio, "bitcrush") ||
        !has_id(list.transitions, "smoothstep")) {
        return "fx_list does not mirror the deployed catalog";
    }

    // fx_audio — bitcrush (4 bits) on a stereo sine block must come
    // back quantized.
    constexpr int kFrames = 1024;
    std::vector<float> pcm(static_cast<std::size_t>(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float s = 0.8f * std::sin(
            2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        pcm[2 * i] = s;
        pcm[2 * i + 1] = s;
    }
    const std::map<std::uint32_t, float> params{{0, 4.0f}};
    std::string audio_reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxAudioRequest(
                    id, fx_dir, "bitcrush", "smoke-clip", pcm.data(),
                    kFrames, 2, 48000, params, 0.0);
            },
            &audio_reply)) {
        return {};   // transport died — tolerated fallback
    }
    std::string err;
    const int audio_state = envelope_ok(audio_reply, &err);
    if (audio_state < 0) {
        return "fx_audio reply is malformed";
    }
    if (audio_state == 0) {
        return {};   // engine unavailable (no V8/catalog) — tolerated
    }
    std::vector<float> out_pcm;
    if (!MediaWorker::DecodeFxAudioReply(audio_reply, &rid,
                                         &out_pcm) ||
        out_pcm.size() != pcm.size()) {
        return "fx_audio payload failed to decode";
    }
    // bits=4 → 16 levels → every sample is a multiple of 1/16, and a
    // sine quantized to 16 levels differs from the input almost
    // everywhere (proving the script actually ran).
    int unquantized = 0;
    int changed = 0;
    for (std::size_t i = 0; i < out_pcm.size(); ++i) {
        const float q16 = out_pcm[i] * 16.0f;
        if (std::fabs(q16 - std::round(q16)) > 0.02f) {
            ++unquantized;
        }
        if (std::fabs(out_pcm[i] - pcm[i]) > 1e-4f) {
            ++changed;
        }
    }
    if (unquantized > 0) {
        return "fx_audio output is not bitcrush-quantized";
    }
    if (changed < static_cast<int>(out_pcm.size() / 2)) {
        return "fx_audio left the block unchanged";
    }

    // fx_transition — smoothstep(0.5) == 0.5.
    std::string trans_reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxTransitionRequest(
                    id, fx_dir, "smoothstep", 0.5);
            },
            &trans_reply)) {
        return {};   // tolerated
    }
    const int trans_state = envelope_ok(trans_reply, &err);
    if (trans_state < 0) {
        return "fx_transition reply is malformed";
    }
    if (trans_state == 0) {
        return {};   // engine unavailable — tolerated
    }
    double weight = -1.0;
    if (!MediaWorker::DecodeFxTransitionReply(trans_reply, &rid,
                                              &weight) ||
        std::fabs(weight - 0.5) > 0.01) {
        return "fx_transition returned the wrong weight";
    }
    return {};
#else
    return {};
#endif
}

// Shared import filter (smoke_checks.h) — the same predicate the
// file dialog's pattern list and the OS file-drop filter are built
// from: common video/audio/still extensions accept (any case),
// obvious non-media and edge cases (no dot, trailing dot, dot inside
// a directory name, near-miss extensions) reject.
std::string CheckMediaImportFilter() {
    for (const char* path :
         {"a.mp4", "B.MP4", "x/song.flac", "pic.PNG", "clip.m2ts",
          "still.dng", "C:\\dir\\clip.mkv"}) {
        if (!IsMediaImportPath(path)) {
            return std::string("rejected media path: ") + path;
        }
    }
    for (const char* path :
         {"a.txt", "b.xerproj", "c.exe", "noext", "trailing.",
          "dir.name/file", "archive.mp4x", "xmp4"}) {
        if (IsMediaImportPath(path)) {
            return std::string("accepted non-media path: ") + path;
        }
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
    {"screenshot_png", &CheckScreenshotPng},
    {"ui_screenshot_diff", &CheckUiScreenshotDiff},
    {"curves_lens", &CheckCurvesAndLens},
    {"js_fx", &CheckJsFx},
    {"audio_mixer", &CheckAudioMixer},
    {"audio_sync", &CheckAudioSync},
    {"project_store", &CheckProjectStore},
    {"project_autosave", &CheckProjectAutosave},
    {"media_catalog", &CheckMediaCatalog},
    {"media_import_filter", &CheckMediaImportFilter},
    {"probe_classification", &CheckProbeClassification},
    {"poster_cache_lru", &CheckPosterCacheLru},
    {"media_worker", &CheckMediaWorker},
    {"sync_worker", &CheckSyncMediaWorker},
    {"fx_worker", &CheckFxScriptWorker},
    {"assets_icons", &CheckAssetsIcons},
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

}  // namespace Smoke
}  // namespace XER::VideoEditor
