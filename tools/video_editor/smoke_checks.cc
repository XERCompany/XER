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
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <json/json.h>

#include "ai/ai_bounce.h"
#include "ai/face_detect.h"
#include "ai/img2img_runner.h"
#include "ai/object_track.h"
#include "ai/rife_interp.h"
#include "ai/sam2_track.h"
#include "ai/silence_detect.h"
#include "ai/silero_vad.h"
#include "ai/smart_reframe.h"
#include "ai/upscale_x4v3.h"
#include "ai/whisper_transcribe.h"
#include "ai_tags.h"
#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "audio_mixer.h"
#include "audio_sync.h"
#include "builtin_fx.h"
#include "captions.h"
#include "cef/cef_ext_scheme.h"
#include "compositor.h"
#include "content/ai/find_object/find_object.h"  // YA.AIFindObject
#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"
#include "lab/object_ops.h"
#include "content/video/video_info.h"
#include "effect_frame_cache.h"
#include "export_engine.h"
#include "export_preset.h"
#include "extensions/ext_host.h"
#include "font_catalog.h"
#include "voice_isolate.h"
#include "frame.h"
#include "frame_exporter.h"
#include "frame_render/frame_render.h"   // YA.FrameRender
#include "generated_source.h"
#include "gpu_video/cuda_decode.h"       // YA.CUDADecode
#include "gpu_video/cuda_encode.h"       // YA.CUDAEncode
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "js_fx.h"
#include "keyboard_map.h"
#include "l2_frame_cache.h"
#include "lut_manager.h"
#include "media_catalog.h"
#include "media_decoder.h"               // MakeGpuDecoder / IMediaDecoder
#include "media_worker_client.h"
#include "media_worker_protocol.h"
#include "nn_probe.h"
#include "photo/adjust_ops.h"
#include "photo/brush.h"
#include "photo/document.h"
#include "photo/layer_ops.h"
#include "photo/mask_ops.h"
#include "photo/photo_history.h"
#include "photo/retouch.h"
#include "photo/selection.h"
#include "photo/tile_store.h"
#include "photo/xerphoto.h"
#include "playback/auto_cacher.h"
#include "playback/preview_engine.h"
#include "plugin_host.h"
#include "poster_cache.h"
#include "project_autosave.h"
#include "project_store.h"
#include "proxy_manager.h"
#include "records_store.h"
#include "rendering/renderer.h"          // Renderer seam for FrameRender check
#include "rendering/render_store.h"      // store-transport smoke path
#include "sequence_preset.h"
#include "source_edit.h"
#include "timecode.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "transcript_edit.h"
#include "transitions_lib.h"
#include "tz/tz.h"
#include "url_import.h"
#include "worker_pipe.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "xer_transition_bridge.h"

#if defined(_WIN32)
#include <windows.h>   // GetModuleFileNameW — worker-exe sibling lookup
// windows.h redefines DrawText -> DrawTextW; rendering/renderer.h was
// included above (before this) so its self-undef already ran and is
// not re-applied under the include guard — drop the macro again for
// the Renderer overrides below.
#ifdef DrawText
    #undef DrawText
#endif
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
    // still images (FFmpeg image2 + the still_image.cc set)
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
// This is a hang bound, not a timing budget: worker-process spawns,
// delay-loaded backend DLL closures (ggml_backend_load_all, the
// ~50-DLL OpenCV set), and the FFmpeg export legitimately take tens
// of seconds on a cold disk / indexer-scanned machine.
constexpr int kWatchdogSeconds = 480;

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

    // Transition junction-handle math: the centered duration clamps
    // to each endpoint's own span AND its available media handles.
    // smoke-c1 [10,12) and smoke-c1b [12,15) are adjacent on V1.
    if (!AddTransition(project, "smoke-c1", "smoke-c1b",
                       Transition::Type::Crossfade, 0.5, &err)) {
        return "AddTransition failed: " + err;
    }
    {
        const Transition& tr = project.transitions.back();
        // Unbounded media handles -> the clip-span bound rules:
        // duration <= 2*min(2s, 3s) = 4s.
        const MediaBounds unknown{};
        const double span_limit =
            TransitionDurationLimit(project, tr, unknown, unknown);
        if (std::fabs(span_limit - 4.0) > 1e-6) {
            return "span-bound transition limit was " +
                   std::to_string(span_limit);
        }
        // 60s media: from's tail handle (60-2=58s) is slack; to's head
        // handle is its 2s in_point -> 4s. Same bound, different side.
        const MediaBounds long_media{60.0, true};
        const double head_limit = TransitionDurationLimit(
            project, tr, long_media, long_media);
        if (std::fabs(head_limit - 4.0) > 1e-6) {
            return "head-handle transition limit was " +
                   std::to_string(head_limit);
        }
        // Shrink the to-clip's source head: in_point 1.0 -> 2s limit,
        // now the media handle is the tighter bound than the span.
        Clip* to = FindClip(project, "smoke-c1b");
        if (to == nullptr) return "smoke-c1b missing";
        const double saved_in = to->in_point;
        to->in_point = 1.0;
        const double media_limit = TransitionDurationLimit(
            project, tr, long_media, long_media);
        to->in_point = saved_in;
        if (std::fabs(media_limit - 2.0) > 1e-6) {
            return "media-handle transition limit was " +
                   std::to_string(media_limit);
        }
    }
    // Duration retarget: valid set applies; non-positive/missing
    // endpoints are rejected without mutating.
    if (!SetTransitionDuration(project, "smoke-c1", "smoke-c1b", 2.0,
                               &err) ||
        std::fabs(project.transitions.back().duration - 2.0) > 1e-9) {
        return "SetTransitionDuration failed: " + err;
    }
    if (SetTransitionDuration(project, "smoke-c1", "smoke-c1b", 0.0,
                              nullptr) ||
        SetTransitionDuration(project, "smoke-c1", "smoke-c1b", -1.0,
                              nullptr) ||
        SetTransitionDuration(project, "smoke-c1", "no-such-clip", 1.0,
                              nullptr)) {
        return "SetTransitionDuration accepted an invalid edit";
    }
    if (std::fabs(project.transitions.back().duration - 2.0) > 1e-9) {
        return "rejected SetTransitionDuration still mutated";
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

    // Junction-handle duration drag commits as ONE undoable step —
    // consecutive duration sets on the same junction coalesce.
    std::string new_id;
    if (!history.SplitClip("smoke-h1", 2.0, &new_id, &err) ||
        new_id.empty()) {
        return "history SplitClip failed: " + err;
    }
    if (!history.AddTransition("smoke-h1", new_id,
                               Transition::Type::Crossfade, 0.5, &err)) {
        return "history AddTransition failed: " + err;
    }
    const auto trans_duration = [&]() {
        for (const auto& t : project.transitions) {
            if (t.from_clip == "smoke-h1" && t.to_clip == new_id) {
                return t.duration;
            }
        }
        return -1.0;
    };
    if (!history.SetTransitionDuration("smoke-h1", new_id, 1.2, &err) ||
        !history.SetTransitionDuration("smoke-h1", new_id, 2.0, &err)) {
        return "history SetTransitionDuration failed: " + err;
    }
    if (std::fabs(trans_duration() - 2.0) > 1e-9) {
        return "SetTransitionDuration did not apply";
    }
    // One undo restores the add-time duration (the two sets merged);
    // the next undo removes the transition itself.
    if (!history.Undo() || std::fabs(trans_duration() - 0.5) > 1e-9) {
        return "undo did not restore the pre-drag duration";
    }
    if (!history.Undo() || !project.transitions.empty()) {
        return "second undo did not remove the transition";
    }
    if (!history.Redo() || !history.Redo() ||
        std::fabs(trans_duration() - 2.0) > 1e-9) {
        return "redo did not restore the dragged duration";
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

// Text-based editing: transcript word spans -> clip source mapping ->
// timeline range -> history_.LiftRange extract. Fully headless — the
// "transcript" is synthesized spans, no whisper/audio involved.
std::string CheckTranscriptEditing() {
    // 1) Segment -> word expansion: text splits on whitespace, each
    // word gets a proportional share of the segment's time span, and
    // word boundaries stay inside the segment.
    std::vector<Ai::TranscriptSegment> segments = {
        {0.0, 4.0, "hello brave new world"},   // 4 words, ~1s each
        {5.0, 6.0, "a bb"},                    // 2 words, 1:2 split
    };
    const auto words = TranscriptWordsFromSegments(segments);
    if (words.size() != 6) {
        return "TranscriptWordsFromSegments produced " +
               std::to_string(words.size()) + " words, want 6";
    }
    if (!words[0].segment_start || !words[4].segment_start ||
        words[1].segment_start || words[5].segment_start) {
        return "segment_start flags are wrong";
    }
    if (std::fabs(words[0].src_t0 - 0.0) > 1e-9 ||
        std::fabs(words[3].src_t1 - 4.0) > 1e-9 ||
        std::fabs(words[4].src_t0 - 5.0) > 1e-9 ||
        std::fabs(words[5].src_t1 - 6.0) > 1e-9) {
        return "word spans do not cover their segments";
    }
    // "a bb" -> 'a' gets 1/3 of the second, 'bb' the rest.
    if (std::fabs((words[4].src_t1 - words[4].src_t0) -
                  (1.0 / 3.0)) > 1e-9) {
        return "word time share is not proportional to length";
    }

    // 2) Clip source->timeline mapping: clip at tl 10, in_point 2,
    // duration 4 @ rate 1 consumes source [2,6).
    Clip clip;
    clip.id = "tr-clip";
    clip.source_path = "media.mp4";
    clip.source.kind = SourceKind::Media;
    clip.timeline_start = 10.0;
    clip.in_point = 2.0;
    clip.duration = 4.0;
    if (std::fabs(SourceToTimeline(clip, 2.5) - 10.5) > 1e-9 ||
        !std::isnan(SourceToTimeline(clip, 1.0)) ||
        !std::isnan(SourceToTimeline(clip, 7.0))) {
        return "SourceToTimeline mapping is wrong";
    }
    // A text selection spanning source [2.5, 5.5) -> tl [10.5, 13.5);
    // a selection outside the clip's consumed span must not map.
    double tl0 = -1.0, tl1 = -1.0;
    if (!SourceRangeToTimeline(clip, 2.5, 5.5, &tl0, &tl1) ||
        std::fabs(tl0 - 10.5) > 1e-9 ||
        std::fabs(tl1 - 13.5) > 1e-9) {
        return "SourceRangeToTimeline mis-mapped the selection";
    }
    if (SourceRangeToTimeline(clip, 0.0, 1.0, &tl0, &tl1)) {
        return "out-of-clip source range still mapped";
    }
    // Clamped intersection: [5.0, 9.0) intersects consumed [2,6) as
    // [5,6) -> tl [13,14).
    if (!SourceRangeToTimeline(clip, 5.0, 9.0, &tl0, &tl1) ||
        std::fabs(tl0 - 13.0) > 1e-9 ||
        std::fabs(tl1 - 14.0) > 1e-9) {
        return "range clamp to clip span is wrong";
    }

    // 3) The mapped range drives the model-level extract the panel's
    // "Ripple delete" issues: history_.LiftRange(close_gap=true)
    // splits the spanner and shifts followers — undo restores all.
    Project project;
    TimelineHistory history(project);
    std::string err;
    if (history.AddTrack(TrackKind::Video, "V1") != 0) {
        return "history AddTrack failed";
    }
    const MediaBounds media{60.0, true};
    Clip a = clip;
    if (!history.AddClip(a, media, nullptr, &err)) {
        return "AddClip a failed: " + err;
    }
    Clip b = clip;
    b.id = "tr-tail";
    b.timeline_start = 16.0;
    b.duration = 2.0;
    if (!history.AddClip(b, media, nullptr, &err)) {
        return "AddClip b failed: " + err;
    }
    // Extract tl [10.5, 13.5) — the same range the panel computed for
    // the "selection" above. Clip a splits; tail shifts left by 3.
    if (!history.LiftRange(10.5, 13.5, true, &err)) {
        return "LiftRange failed: " + err;
    }
    const Clip* tail = FindClip(project, "tr-tail");
    if (tail == nullptr || std::fabs(tail->timeline_start - 13.0) > 1e-9) {
        return "extract did not shift the follower clip";
    }
    // Clip a's remains: [10,10.5) + [10.5,11) — two 0.5 s pieces
    // before the tail's new start at 13.
    int pieces = 0;
    double covered = 0.0;
    for (const Clip& c : project.clips) {
        if (c.track == 0 && c.timeline_start < 13.0) {
            ++pieces;
            covered += c.duration;
        }
    }
    if (pieces != 2 || std::fabs(covered - 1.0) > 1e-9) {
        return "extract left the wrong clip pieces behind";
    }
    if (!history.Undo()) return "LiftRange undo failed";
    if (project.clips.size() != 2 ||
        std::fabs(FindClip(project, "tr-clip")->timeline_start -
                  10.0) > 1e-9) {
        return "undo did not restore the pre-extract project";
    }

    // 4) Per-media cache: save/load round-trips spans and rejects a
    // size-mismatched (replaced) media file.
    const auto dir = SmokeTempDir() / "transcript_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto media_path = dir / "talk.wav";
    {
        std::ofstream m(media_path, std::ios::binary | std::ios::trunc);
        m.write("0123456789", 10);   // 10-byte stand-in media
    }
    const std::string media_utf8 = Utf8Path(media_path);
    const std::string key = TranscriptCacheKey(media_utf8);
    if (key.size() != 16) return "cache key is not a 16-hex id";
    const auto json_path = dir / (key + ".json");
    if (!SaveTranscriptCache(Utf8Path(json_path), media_utf8,
                             segments)) {
        return "SaveTranscriptCache failed";
    }
    std::vector<Ai::TranscriptSegment> loaded;
    if (!LoadTranscriptCache(Utf8Path(json_path), media_utf8,
                             &loaded) ||
        loaded.size() != segments.size() ||
        loaded[0].text != segments[0].text) {
        return "LoadTranscriptCache round-trip failed";
    }
    // Grow the file -> the key and the size check both move on.
    {
        std::ofstream m(media_path,
                        std::ios::binary | std::ios::app);
        m.write("x", 1);
    }
    if (TranscriptCacheKey(media_utf8) == key) {
        return "cache key ignored the media size";
    }
    if (LoadTranscriptCache(Utf8Path(json_path), media_utf8,
                            &loaded)) {
        return "stale (size-mismatched) cache entry was served";
    }
    return {};
}

// Animated captions (captions.h): real whisper word timings ->
// word-level clip generation per preset + the karaoke style tokens
// the renderer animates against clip-local time.
std::string CheckCaptions() {
    // Real token timings ride TranscriptSegment::words straight
    // through — no proportional redistribution.
    std::vector<Ai::TranscriptSegment> timed = {
        {0.0, 2.0, "one two",
         {{0.1, 0.4, "one"}, {0.9, 1.8, "two"}}},
    };
    const auto timed_words = TranscriptWordsFromSegments(timed);
    if (timed_words.size() != 2 ||
        std::fabs(timed_words[0].src_t0 - 0.1) > 1e-9 ||
        std::fabs(timed_words[1].src_t1 - 1.8) > 1e-9 ||
        !timed_words[0].segment_start) {
        return "real word timings were not used verbatim";
    }

    // Bound media clip: tl 10, in_point 0, rate 1.
    Clip bound;
    bound.id = "cap-src";
    bound.source_path = "m.mp4";
    bound.source.kind = SourceKind::Media;
    bound.timeline_start = 10.0;
    bound.in_point = 0.0;
    bound.duration = 60.0;

    std::vector<Ai::TranscriptSegment> segments = {
        {0.0, 4.0, "hello brave new world"},   // 4 words
        {5.0, 6.0, "a bb"},                    // 2 words
    };
    const auto words = TranscriptWordsFromSegments(segments);

    CaptionStyle style;
    // Classic — one clip per segment, flat style.
    style.preset = CaptionStyle::Preset::Classic;
    style.animate = false;
    const auto clips =
        BuildCaptionClips(words, bound, 0.0, 10.0, 3, style);
    if (clips.size() != 2) {
        return "classic preset made " + std::to_string(clips.size()) +
               " clips, want 2";
    }
    if (std::fabs(clips[0].timeline_start - 10.0) > 1e-9 ||
        std::fabs(clips[0].duration - 4.0) > 1e-9 ||
        std::fabs(clips[1].timeline_start - 15.0) > 1e-9 ||
        clips[0].track != 3) {
        return "classic clip mapping is wrong";
    }
    const ClipTextStyle classic_style =
        ParseClipTextStyle(clips[0].source.param);
    if (classic_style.karaoke ||
        classic_style.text != "hello brave new world" ||
        classic_style.size_px != 56) {
        return "classic caption style param is wrong";
    }
    // Selection filtering: only the second segment intersects
    // [5,7) -> exactly one clip.
    if (BuildCaptionClips(words, bound, 5.0, 7.0, 3, style).size() !=
        1) {
        return "source-range filter kept wrong segments";
    }

    // Karaoke — one clip per segment carrying a clip-local word span
    // table; word indices are positional over the joined text.
    style.preset = CaptionStyle::Preset::Karaoke;
    const auto kclips =
        BuildCaptionClips(words, bound, 0.0, 10.0, 3, style);
    if (kclips.size() != 2) {
        return "karaoke preset made " +
               std::to_string(kclips.size()) + " clips, want 2";
    }
    const ClipTextStyle ks =
        ParseClipTextStyle(kclips[0].source.param);
    if (!ks.karaoke || ks.word_times.size() != 4) {
        return "karaoke style lost its word span table";
    }
    // Chained spans: word i+1 starts where word i ended (proportional
    // distribution is gapless).
    if (ks.word_times[0].t0 != 0.0f ||
        ks.word_times[3].t1 < 3.9f ||
        std::fabs(ks.word_times[1].t0 - ks.word_times[0].t1) >
            0.01f) {
        return "karaoke word spans do not chain";
    }
    if (kclips[0].source.param.find("kact=") == std::string::npos ||
        kclips[0].source.param.find("kwt=") == std::string::npos) {
        return "karaoke tokens missing from clip param";
    }

    // WordPop — one clip per word, pop keyframes on each.
    style.preset = CaptionStyle::Preset::WordPop;
    style.words_per_clip = 1;
    style.animate = true;
    const auto pclips =
        BuildCaptionClips(words, bound, 0.0, 10.0, 3, style);
    if (pclips.size() != 6) {
        return "wordpop preset made " +
               std::to_string(pclips.size()) + " clips, want 6";
    }
    const ClipTextStyle ps =
        ParseClipTextStyle(pclips[0].source.param);
    if (ps.text != "hello" ||
        pclips[0].keyframes.count(KeyProp::Scale) == 0 ||
        pclips[0].keyframes.count(KeyProp::Opacity) == 0 ||
        std::fabs(pclips[0].timeline_start - 10.0) > 1e-9) {
        return "wordpop clip shape is wrong";
    }
    // Chunking: 2 words per clip -> 2 clips for the 4-word segment.
    style.words_per_clip = 2;
    if (BuildCaptionClips(words, bound, 0.0, 4.5, 3, style).size() !=
        2) {
        return "word chunking is wrong";
    }

    // The karaoke render is time-variant: identical style, different
    // clip-local times -> different pixels (both rasterizers honor
    // the buckets).
    const ClipTextStyle anim = ParseClipTextStyle(
        "one two | size=24 karaoke=1 kact=#FF0000 "
        "kwt=0.1-0.5,0.5-1.0");
    if (!anim.karaoke || anim.word_times.size() != 2) {
        return "kwt token did not parse";
    }
    video::Frame fa, fb, fc;
    if (!RenderClipText(anim, 200, &fa, 0.0) ||
        !RenderClipText(anim, 200, &fb, 0.2) ||
        !RenderClipText(anim, 200, &fc, 0.7)) {
        return "karaoke frame render failed";
    }
    const auto same_pixels = [](const video::Frame& a,
                                const video::Frame& b) {
        return a.DataSize() == b.DataSize() &&
               std::memcmp(a.Data(), b.Data(), a.DataSize()) == 0;
    };
    if (same_pixels(fa, fb) || same_pixels(fb, fc)) {
        return "karaoke render did not vary with clip-local time";
    }

    // Transcript cache v2 round-trips the word timings.
    const auto dir = SmokeTempDir() / "transcript_cache_words";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto media_path = dir / "talk.wav";
    {
        std::ofstream m(media_path,
                        std::ios::binary | std::ios::trunc);
        m.write("0123456789", 10);
    }
    const std::string media_utf8 = Utf8Path(media_path);
    const auto wjson_path =
        dir / (TranscriptCacheKey(media_utf8) + ".json");
    if (!SaveTranscriptCache(Utf8Path(wjson_path), media_utf8,
                             timed)) {
        return "SaveTranscriptCache(v2) failed";
    }
    std::vector<Ai::TranscriptSegment> wloaded;
    if (!LoadTranscriptCache(Utf8Path(wjson_path), media_utf8,
                             &wloaded) ||
        wloaded.size() != 1 || wloaded[0].words.size() != 2 ||
        std::fabs(wloaded[0].words[1].t1 - 1.8) > 1e-9) {
        return "cached word timings did not round-trip";
    }
    return {};
}

// Ai::TranscribeMaxSeconds (ai/whisper_transcribe.h) — the named
// bound that replaced the hard-coded 10-minute PCM cap. The bound
// itself is kept on purpose: ExtractMonoPcm materializes the whole
// span (~3.7 MiB/min at 16 kHz mono) and whisper walks it in one
// job, so the cap bounds memory + worker residency. Default 60 min;
// XER_TRANSCRIBE_MAX_MINUTES overrides it, <= 0 un-caps, and an
// unparseable value must fall back — not silently un-cap.
std::string CheckTranscribeCap() {
    // Portable env helpers — POSIX setenv/unsetenv don't exist in the
    // UCRT; _putenv_s is the copying equivalent ("" value unsets).
    const auto set_env = [](const char* name, const char* value) {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    };
    const auto unset_env = [](const char* name) {
#ifdef _WIN32
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    };
    const char* prev = std::getenv("XER_TRANSCRIBE_MAX_MINUTES");
    const std::string saved = prev != nullptr ? prev : "";
    const auto check = [&]() -> std::string {
        unset_env("XER_TRANSCRIBE_MAX_MINUTES");
        if (Ai::TranscribeMaxSeconds() != 3600.0) {
            return "default transcribe cap is not 60 minutes";
        }
        set_env("XER_TRANSCRIBE_MAX_MINUTES", "5");
        if (Ai::TranscribeMaxSeconds() != 300.0) {
            return "env override (minutes) not honored";
        }
        set_env("XER_TRANSCRIBE_MAX_MINUTES", "0");
        if (Ai::TranscribeMaxSeconds() != 0.0) {
            return "zero override did not un-cap the decode";
        }
        set_env("XER_TRANSCRIBE_MAX_MINUTES", "not-a-number");
        if (Ai::TranscribeMaxSeconds() != 3600.0) {
            return "unparseable override did not fall back to default";
        }
        return {};
    };
    const std::string result = check();
    // Restore whatever the runner had — the suite runs in-process.
    if (saved.empty()) {
        unset_env("XER_TRANSCRIBE_MAX_MINUTES");
    } else {
        set_env("XER_TRANSCRIBE_MAX_MINUTES", saved.c_str());
    }
    return result;
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

// Extended text-effect style set (text_style.h): a fully styled text
// clip must render non-empty pixels, the param serializer must round
// trip through the parser, and — when the build links Skia — the
// system font catalog must enumerate at least one family.
std::string CheckTextStyleExt() {
    Clip styled = MakeGeneratedClip(
        "styled", 0, 0.0, 1.0, SourceKind::Text,
        "Smoke Extended | font=Arial weight=700 italic=1 underline=1 "
        "size=28 tracking=1.5 align=center valign=middle x=4 y=2 "
        "w=300 h=80 radius=8 color=#FF8800FF opacity=0.95 outline=1 "
        "outline_color=#000000FF bg=#20202080 shadow=1 shadow_x=3 "
        "shadow_y=3 shadow_blur=2 shadow_color=#000000AA baseline=0");
    video::Frame styled_frame;
    if (!GenerateSourceFrame(styled, 0.0, 320, 180, &styled_frame) ||
        !styled_frame.IsValid()) {
        return "extended text clip failed to generate";
    }
    const bool any_pixels =
        std::any_of(styled_frame.Data(),
                    styled_frame.Data() + styled_frame.DataSize(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "extended text clip rendered fully transparent";
    }
    // ...versus an empty text clip, which stays a 1x1 zero frame.
    Clip empty = MakeGeneratedClip("empty", 0, 0.0, 1.0,
                                   SourceKind::Text, "");
    video::Frame empty_frame;
    if (!GenerateSourceFrame(empty, 0.0, 320, 180, &empty_frame) ||
        !empty_frame.IsValid()) {
        return "empty text clip failed to generate";
    }
    if (empty_frame.DataSize() == styled_frame.DataSize() &&
        std::memcmp(empty_frame.Data(), styled_frame.Data(),
                    empty_frame.DataSize()) == 0) {
        return "styled text renders identically to empty text";
    }

    // Parse -> Serialize -> Parse round trip on every new field.
    const ClipTextStyle a = ParseClipTextStyle(styled.source.param);
    const ClipTextStyle b =
        ParseClipTextStyle(SerializeClipTextStyle(a));
    if (b.text != a.text || b.font_family != a.font_family ||
        b.font_weight != a.font_weight || b.italic != a.italic ||
        b.underline != a.underline || b.strike != a.strike ||
        b.size_px != a.size_px ||
        std::fabs(b.letter_spacing - a.letter_spacing) > 0.01f ||
        std::fabs(b.line_spacing - a.line_spacing) > 0.01f ||
        b.align != a.align || b.valign != a.valign ||
        std::fabs(b.pos_x - a.pos_x) > 0.01f ||
        std::fabs(b.pos_y - a.pos_y) > 0.01f ||
        std::fabs(b.box_w - a.box_w) > 0.01f ||
        std::fabs(b.box_h - a.box_h) > 0.01f ||
        std::fabs(b.corner_radius - a.corner_radius) > 0.01f ||
        std::fabs(b.rotate_deg - a.rotate_deg) > 0.01f ||
        std::fabs(b.baseline_px - a.baseline_px) > 0.01f ||
        b.color != a.color ||
        std::fabs(b.fill_opacity - a.fill_opacity) > 0.01f ||
        std::fabs(b.outline_px - a.outline_px) > 0.01f ||
        b.outline_color != a.outline_color || b.bg_color != a.bg_color ||
        b.shadow_explicit != a.shadow_explicit ||
        std::fabs(b.shadow_x - a.shadow_x) > 0.01f ||
        std::fabs(b.shadow_y - a.shadow_y) > 0.01f ||
        std::fabs(b.shadow_blur - a.shadow_blur) > 0.01f ||
        b.shadow_color != a.shadow_color) {
        return "text style round trip mismatch: " +
               SerializeClipTextStyle(b);
    }
    // A family name with spaces must survive the token grammar.
    const ClipTextStyle spaced = ParseClipTextStyle(
        "T | font=Segoe%20UI weight=400");
    if (spaced.font_family != "Segoe UI") {
        return "percent-encoded font family mis-decoded: " +
               spaced.font_family;
    }

    if (FontCatalog::Available() && FontCatalog::FamilyCount() <= 0) {
        return "font catalog enumerated zero families";
    }
    std::fprintf(stderr, "  font catalog: %d families (skia %s)\n",
                 FontCatalog::FamilyCount(),
                 FontCatalog::Available() ? "on" : "off");
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

// Named layer blend modes: every BlendMode must route through
// ApplyBlendModePixels -> MixSystem::BlendFrames and produce the W3C
// result. Expected bytes are hand-computed from the W3C formulas on a
// two-pixel fixture (tolerance 1 for float-vs-int rounding).
std::string CheckBlendModes() {
    const std::array<std::uint8_t, 4> kD = {100, 150, 200, 255};  // b,g,r,a
    const std::array<std::uint8_t, 4> kS = {50, 100, 150, 255};
    auto make = [](const std::array<std::uint8_t, 4>& px) {
        video::Frame f;
        if (!f.Allocate(1, 1, video::PixelFormat::BGRA8)) return f;
        std::memcpy(f.Data(), px.data(), 4);
        return f;
    };
    auto run = [&](BlendMode m, const std::array<std::uint8_t, 4>& s_px) {
        video::Frame d = make(kD);
        const video::Frame s = make(s_px);
        if (!ApplyBlendModePixels(m, &d, s)) return video::Frame{};
        return d;
    };
    auto expect = [](const video::Frame& f,
                     const std::array<std::uint8_t, 4>& want, int tol,
                     const char* what) -> std::string {
        if (!f.IsValid()) {
            return std::string(what) + ": blend returned false";
        }
        for (int c = 0; c < 4; ++c) {
            const int got = f.Data()[c];
            const int exp = want[static_cast<std::size_t>(c)];
            if (std::abs(got - exp) > tol) {
                return std::string(what) + ": channel " +
                       std::to_string(c) + " got " + std::to_string(got) +
                       " want ~" + std::to_string(exp);
            }
        }
        return {};
    };
    struct Case {
        BlendMode mode;
        std::array<std::uint8_t, 4> want;   // bytes b,g,r,a
        int tol;
        const char* name;
    };
    const Case kCases[] = {
        // Opaque-src-over-opaque-dst fixtures — the composite is exact.
        {BlendMode::Normal,     kS,                 0, "normal"},
        {BlendMode::SrcOver,    kS,                 0, "src_over"},
        {BlendMode::Src,        kS,                 0, "src"},
        {BlendMode::SrcIn,      kS,                 1, "src_in"},
        {BlendMode::SrcATop,    kS,                 1, "src_atop"},
        {BlendMode::Dst,        kD,                 0, "dst"},
        {BlendMode::DstIn,      kD,                 1, "dst_in"},
        {BlendMode::DstATop,    kD,                 1, "dst_atop"},
        {BlendMode::Clear,      {0, 0, 0, 0},       0, "clear"},
        {BlendMode::SrcOut,     {0, 0, 0, 0},       1, "src_out"},
        {BlendMode::DstOut,     {0, 0, 0, 0},       1, "dst_out"},
        {BlendMode::Xor,        {0, 0, 0, 0},       1, "xor"},
        {BlendMode::Add,        {150, 250, 255, 255}, 1, "add"},
        {BlendMode::Plus,       {150, 250, 255, 255}, 1, "plus"},
        {BlendMode::Multiply,   {20, 59, 118, 255}, 1, "multiply"},
        {BlendMode::Modulate,   {20, 59, 118, 255}, 1, "modulate"},
        {BlendMode::Screen,     {130, 191, 232, 255}, 1, "screen"},
        {BlendMode::Overlay,    {39, 127, 210, 255},  1, "overlay"},
        {BlendMode::Darken,     {50, 100, 150, 255}, 0, "darken"},
        {BlendMode::Lighten,    {100, 150, 200, 255}, 0, "lighten"},
        {BlendMode::ColorDodge, {124, 247, 255, 255}, 1, "color_dodge"},
        {BlendMode::ColorBurn,  {0, 0, 161, 255},  1, "color_burn"},
        {BlendMode::HardLight,  {39, 118, 210, 255}, 1, "hard_light"},
        {BlendMode::SoftLight,  {63, 137, 205, 255}, 1, "soft_light"},
        {BlendMode::Difference, {50, 50, 50, 255}, 1, "difference"},
        {BlendMode::Exclusion,  {111, 132, 115, 255}, 1, "exclusion"},
        {BlendMode::Hue,        {100, 150, 200, 255}, 1, "hue"},
        {BlendMode::Saturation, {100, 150, 200, 255}, 1, "saturation"},
        {BlendMode::Color,      {100, 150, 200, 255}, 1, "color"},
        {BlendMode::Luminosity, {50, 100, 150, 255},  1, "luminosity"},
    };
    for (const Case& c : kCases) {
        const std::string fail =
            expect(run(c.mode, kS), c.want, c.tol, c.name);
        if (!fail.empty()) {
            return fail;
        }
    }
    // Translucent source: src-over alpha must blend to a=255 over an
    // opaque backdrop with weighted color.
    const std::array<std::uint8_t, 4> kHalf = {50, 100, 150, 128};
    const video::Frame translucent = run(BlendMode::SrcOver, kHalf);
    const std::string fail = expect(translucent, {75, 125, 175, 255}, 1,
                                    "translucent src_over");
    if (!fail.empty()) {
        return fail;
    }
    return {};
}
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

// Preview-quality hint: Compositor::SetPreviewQuality(<1) runs the
// heavy glass/bokeh kernels on a downscaled frame and upsamples back
// — the result must stay close to (but not identical with) the full
// render, and at 1080p bokeh the preview pass must be measurably
// faster. Quality 1.0 must be byte-identical to the untouched
// default (the export/screenshot path depends on it).
std::string CheckPreviewQualityScale() {
    std::string err;

    // 1) Identity: quality 1.0 == never-set — byte for byte.
    {
        Project small = MakeRenderProject(160, 90, 15);
        small.clips[0].effects = {
            {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.1f, 0.4f, 8.0f, 2.0f}},
        };
        Compositor def;
        if (!def.Init(small, {}, &err)) {
            return "identity compositor init failed: " + err;
        }
        std::vector<std::uint8_t> ref;
        if (!def.RenderFrameAt(0.5, &ref, &err)) {
            return "identity reference render failed: " + err;
        }
        def.SetPreviewQuality(1.0f);
        std::vector<std::uint8_t> same;
        if (!def.RenderFrameAt(0.5, &same, &err)) {
            return "quality-1.0 render failed: " + err;
        }
        def.Shutdown();
        if (ref.size() != same.size() ||
            std::memcmp(ref.data(), same.data(), ref.size()) != 0) {
            return "SetPreviewQuality(1.0) is not pixel-identical";
        }
    }

    // 2) 1080p bokeh: preview render is faster and close-but-not-equal.
    {
        Project project = MakeRenderProject(1920, 1080, 30);
        project.clips[0].effects = {
            {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.12f, 0.3f, 18.0f, 3.0f}},
        };
        Compositor comp;
        if (!comp.Init(project, {}, &err)) {
            return "preview compositor init failed: " + err;
        }
        std::vector<std::uint8_t> full, half;
        const auto t0 = std::chrono::steady_clock::now();
        if (!comp.RenderFrameAt(0.5, &full, &err)) {
            return "full-quality render failed: " + err;
        }
        const double full_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0)
                .count();
        comp.SetPreviewQuality(0.5f);
        const auto t1 = std::chrono::steady_clock::now();
        if (!comp.RenderFrameAt(0.5, &half, &err)) {
            return "half-quality render failed: " + err;
        }
        const double half_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t1)
                .count();
        comp.Shutdown();
        if (half.size() != full.size()) {
            return "preview render changed the frame size";
        }
        // The reduced-res path must actually WIN — the whole point of
        // the hint. The ~4x expected margin makes this robust to
        // ordinary timing noise on a loaded machine.
        if (half_ms >= full_ms) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "preview path no faster (full %.0f ms vs "
                          "half %.0f ms)", full_ms, half_ms);
            return buf;
        }
        // Close but not identical: resampling is lossy, so the frames
        // must differ a little; if they matched exactly the hint
        // didn't engage, and a wild divergence means a broken kernel.
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < full.size(); ++i) {
            sum += static_cast<std::uint64_t>(std::abs(
                static_cast<int>(full[i]) - static_cast<int>(half[i])));
        }
        const double mean =
            static_cast<double>(sum) / static_cast<double>(full.size());
        std::fprintf(stderr,
                     "  preview_quality: full %.0f ms, half %.0f ms, "
                     "mean|diff| %.3f/byte\n",
                     full_ms, half_ms, mean);
        if (mean <= 0.001) {
            return "preview output identical to full — hint ignored?";
        }
        if (mean > 20.0) {
            return "preview output diverged too far (mean " +
                   std::to_string(mean) + ")";
        }
    }
    return {};
}

// Effect-chain result cache (effect_frame_cache.h — the Olive-style
// hash cache wired into Compositor via CompositorOptions::frame_cache).
// Verifies, on the headless generated-bars path:
//   * a repeat render of the same clip/time/params is a cache hit and
//     byte-identical;
//   * an effect-param change produces a miss AND different pixels
//     (invalidation — stale frames are the worst bug class);
//   * a different clip-local time is a miss (time is key material);
//   * an effect-free clip stores nothing (cheap chains stay off the
//     fast path).
std::string CheckFxFrameCache() {
    auto make_project = [](float brightness) {
        Project p = MakeRenderProject(160, 90, 15);
        p.clips[0].effects = {
            {"brightness_contrast", {brightness, 1.2f}},
            {"grayscale", {}},
        };
        return p;
    };
    auto cache =
        std::make_shared<EffectFrameCache>(64ull * 1024ull * 1024ull);
    CompositorOptions opts;
    opts.frame_cache = cache;
    std::string err;

    // Pass 1: first render stores; the immediate repeat is served.
    Compositor a;
    if (!a.Init(make_project(40.0f), opts, &err)) {
        return "cache compositor A init failed: " + err;
    }
    std::vector<std::uint8_t> a1, a2;
    if (!a.RenderFrameAt(0.5, &a1, &err)) {
        return "render A1 failed: " + err;
    }
    if (!a.RenderFrameAt(0.5, &a2, &err)) {
        return "render A2 failed: " + err;
    }
    a.Shutdown();
    if (a1 != a2) {
        return "repeat render produced different pixels";
    }
    auto stats = cache->GetStats();
    if (stats.stores < 1) {
        return "post-effect frame was not stored";
    }
    // A fresh compositor on the SAME cache must hit the stored entry —
    // this is the seek/reopen case the cache exists for.
    Compositor b;
    if (!b.Init(make_project(40.0f), opts, &err)) {
        return "cache compositor B init failed: " + err;
    }
    std::vector<std::uint8_t> b1;
    if (!b.RenderFrameAt(0.5, &b1, &err)) {
        return "render B1 failed: " + err;
    }
    b.Shutdown();
    if (a1 != b1) {
        return "cache hit returned different pixels";
    }
    stats = cache->GetStats();
    if (stats.hits < 1) {
        return "shared-cache render was not a hit";
    }
    // Invalidation: same clip id, tweaked effect param -> miss + new
    // pixels under a different key.
    Compositor c;
    if (!c.Init(make_project(90.0f), opts, &err)) {
        return "cache compositor C init failed: " + err;
    }
    std::vector<std::uint8_t> c1;
    if (!c.RenderFrameAt(0.5, &c1, &err)) {
        return "render C1 failed: " + err;
    }
    c.Shutdown();
    if (a1 == c1) {
        return "param change produced identical pixels";
    }
    // Invalidation by time: identical params at a different clip-local
    // time must miss (u is key material for generated sources).
    Compositor d;
    if (!d.Init(make_project(40.0f), opts, &err)) {
        return "cache compositor D init failed: " + err;
    }
    std::vector<std::uint8_t> d1;
    if (!d.RenderFrameAt(0.7, &d1, &err)) {
        return "render D1 failed: " + err;
    }
    d.Shutdown();
    stats = cache->GetStats();
    if (stats.misses < 2) {
        return "param/time changes did not miss (misses=" +
               std::to_string(stats.misses) + ")";
    }
    // Eligibility: an effect-free clip renders without storing.
    const std::uint64_t stores_before = stats.stores;
    Project plain = MakeRenderProject(160, 90, 15);   // no effects
    Compositor e;
    if (!e.Init(plain, opts, &err)) {
        return "cache compositor E init failed: " + err;
    }
    std::vector<std::uint8_t> e1;
    if (!e.RenderFrameAt(0.5, &e1, &err)) {
        return "render E1 failed: " + err;
    }
    e.Shutdown();
    stats = cache->GetStats();
    if (stats.stores != stores_before) {
        return "effect-free clip polluted the cache";
    }
    std::fprintf(stderr,
                 "  fx_frame_cache: %llu hits %llu misses %llu stores "
                 "%llu evictions, %llu B in %llu entries\n",
                 (unsigned long long)stats.hits,
                 (unsigned long long)stats.misses,
                 (unsigned long long)stats.stores,
                 (unsigned long long)stats.evictions,
                 (unsigned long long)stats.bytes,
                 (unsigned long long)stats.entries);
    return {};
}

// Olive-style idle-time pre-render (playback/auto_cacher.{h,cc} —
// the AutoCacher blueprint in
// docs/wiki/research/2026-11-21-nle-architectures.md §4). The worker
// owns a PRIVATE compositor (the only safe seam — Compositor is
// single-consumer-thread) and fills the SHARED EffectFrameCache, so
// a playback-path render at a primed frame time is a cache hit.
// Verifies, headless on the generated-bars path:
//   * a settled seek primes a frame window (stats.done > 0, cache
//     stores grow);
//   * a fresh compositor on the same cache rendering primed frame
//     times HITS — the "playback frames become cache hits" contract;
//   * a state change (effect-param edit arrives as a re-Init)
//     produces misses + fresh stores — no stale serves;
//   * the engine wiring: PreviewEngine seeks drive OnSeek, stepped
//     playback hits FrameCacheStats, and a quality flip reschedules
//     (preview quality is key material).
std::string CheckAutoCache() {
    constexpr double kDt = 1.0 / 15.0;
    auto make_project = [](float brightness) {
        Project p;
        p.width = 160;
        p.height = 90;
        p.fps_num = 15;
        p.fps_den = 1;
        p.tracks.push_back(Track{TrackKind::Video, "V1"});
        p.tracks.push_back(Track{TrackKind::Audio, "A1"});
        Clip clip = MakeGeneratedClip("bars", 0, 0.0, 4.0,
                                      SourceKind::ColorBars, "smpte75");
        clip.effects = {
            {"brightness_contrast", {brightness, 1.2f}},
            {"grayscale", {}},
        };
        p.clips.push_back(clip);
        return p;
    };
    std::string err;
    auto cache =
        std::make_shared<EffectFrameCache>(64ull * 1024ull * 1024ull);

    // ---- phase A: direct AutoCacher priming -> playback hits ------
    {
        AutoCacher ac;
        AutoCacher::Options aco;
        aco.frame_cache = cache;
        aco.settle_seconds = 0.05;   // fast settle for the check
        if (!ac.Init(make_project(40.0f), aco, &err)) {
            return "autocache init failed: " + err;
        }
        ac.OnSeek(0.5);
        if (!ac.WaitIdle(15000)) {
            return "autocache never went idle after seek";
        }
        AutoCacher::Stats astats = ac.GetStats();
        if (astats.done < 1 || astats.queued < 1 ||
            astats.windows < 1) {
            return "autocache primed nothing (done=" +
                   std::to_string(astats.done) + ")";
        }
        auto stats = cache->GetStats();
        if (stats.stores < 1) {
            return "autocache stored no frames";
        }
        const std::uint64_t stores_after_prime = stats.stores;

        // "Playback": a fresh compositor on the same cache rendering
        // primed frame times must hit. The window around frame 7
        // (t=0.5) covered [0, 52] — render its head.
        CompositorOptions cco;
        cco.frame_cache = cache;
        Compositor playback;
        if (!playback.Init(make_project(40.0f), cco, &err)) {
            return "playback compositor init failed: " + err;
        }
        std::vector<std::uint8_t> px;
        for (int n = 8; n <= 12; ++n) {
            if (!playback.RenderFrameAt(n * kDt, &px, &err)) {
                return "playback render failed: " + err;
            }
        }
        playback.Shutdown();
        stats = cache->GetStats();
        if (stats.hits < 5) {
            return "primed frames were not hits (hits=" +
                   std::to_string(stats.hits) + ")";
        }

        // Invalidation: a param edit arrives as a re-Init snapshot —
        // the same frame times under a new state hash must MISS and
        // store fresh (never serve stale).
        if (!ac.Init(make_project(90.0f), aco, &err)) {
            return "autocache re-init failed: " + err;
        }
        ac.OnSeek(0.5);
        if (!ac.WaitIdle(15000)) {
            return "autocache never went idle after re-init";
        }
        astats = ac.GetStats();
        stats = cache->GetStats();
        if (astats.done < 1 || stats.stores <= stores_after_prime) {
            return "post-edit re-prime stored nothing new";
        }
        std::fprintf(
            stderr,
            "  auto_cache(direct): %llu windows %llu queued %llu done "
            "%llu skipped %llu cancelled %llu failed\n",
            (unsigned long long)astats.windows,
            (unsigned long long)astats.queued,
            (unsigned long long)astats.done,
            (unsigned long long)astats.skipped,
            (unsigned long long)astats.cancelled,
            (unsigned long long)astats.failed);
    }

    // ---- phase B: engine wiring (env-gated like production) -------
    if (!AutoCacher::EnabledByEnv()) {
        std::fprintf(stderr,
                     "  auto_cache(engine): skipped — "
                     "XER_VE_AUTOCACHE=0\n");
        return {};
    }
    struct NullSink : PreviewEngine::Sink {
        void OnVideoFrame(
            Render::scoped_refptr<Render::VideoFrame>) override {
            ++frames;
        }
        void OnAudioPeakLevels(float, float) override {}
        void OnPlayhead(double) override {}
        std::atomic<int> frames{0};
    } sink;
    PreviewEngine engine;
    PreviewOptions po;
    po.audio_enabled = false;   // video-only — no sink needed
    if (!engine.Init(make_project(40.0f), po, &sink, &err)) {
        return "engine init failed: " + err;
    }
    engine.Seek(1.0);
    if (!engine.WaitAutoCacheIdle(15000)) {
        return "engine autocache never went idle";
    }
    AutoCacher::Stats estats = engine.AutoCacheStats();
    if (estats.done < 1 || estats.windows < 1) {
        return "engine autocache primed nothing (done=" +
               std::to_string(estats.done) + ")";
    }
    // Stepped playback over primed frame times — each seek presents
    // the landing frame; primed frames must read as cache hits. The
    // seeks are paced one-present-at-a-time: back-to-back Seek calls
    // coalesce on seek_pending_ and only the last would render.
    const std::uint64_t hits_before = engine.FrameCacheStats().hits;
    for (int n = 15; n <= 18; ++n) {
        const int frames_before = sink.frames.load();
        engine.Seek(n * kDt);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
        while (sink.frames.load() <= frames_before &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (sink.frames.load() <= frames_before) {
            return "seek present timed out";
        }
    }
    const std::uint64_t hits_after = engine.FrameCacheStats().hits;
    if (hits_after <= hits_before) {
        return "stepped playback produced no cache hits";
    }
    if (!engine.WaitAutoCacheIdle(15000)) {
        return "engine autocache never settled after seeks";
    }
    // Quality flip = state change: a new window must be scheduled at
    // the new tier (the old keys are unreachable).
    estats = engine.AutoCacheStats();
    const std::uint64_t windows_before = estats.windows;
    engine.SetQuality(0.5f);
    if (!engine.WaitAutoCacheIdle(15000)) {
        return "engine autocache never settled after quality flip";
    }
    estats = engine.AutoCacheStats();
    if (estats.windows <= windows_before) {
        return "quality change did not reschedule a window";
    }
    std::fprintf(stderr,
                 "  auto_cache(engine): %llu windows %llu queued "
                 "%llu done %llu skipped %llu cancelled %llu failed; "
                 "playback hits %llu->%llu\n",
                 (unsigned long long)estats.windows,
                 (unsigned long long)estats.queued,
                 (unsigned long long)estats.done,
                 (unsigned long long)estats.skipped,
                 (unsigned long long)estats.cancelled,
                 (unsigned long long)estats.failed,
                 (unsigned long long)hits_before,
                 (unsigned long long)hits_after);
    engine.Shutdown();
    return {};
}

// ---- parallel_determinism --------------------------------------------
// VeParallelRows must be byte-deterministic: every wrapped kernel writes
// disjoint output rows, so serial vs pooled dispatch must produce
// identical pixels. Renders one frame of a project exercising every
// parallel family — a crossfade transition over the cut, a feathered
// clip mask, a rotated+scaled+offset warp, a non-Normal blend mode on
// an upper layer, and two builtin per-pixel kernels — once under
// XER_VE_THREADS=1 (serial path, pool never engaged) and once under
// the full pool, then memcmps. XER_VE_THREADS is read per dispatch
// (parallel_rows.cc VeRenderThreadCount) so both configs live in one
// process; the serial pass runs FIRST so its bytes can never have been
// produced by a worker. Wall ms for both passes is printed so the
// check log shows the speedup alongside the proof.
std::string CheckParallelDeterminism() {
    const auto build = [] {
        Project project = MakeRenderProject(960, 540, 15);
        project.tracks.push_back(Track{TrackKind::Video, "V2"});
        Clip& a = project.clips[0];                    // bars [0,2)
        a.effects.push_back({"builtin:xer.fx.film_grain",
                             {30.f, 1.f, 2.f, 5.f}});
        a.effects.push_back({"grayscale", {}});
        a.transform.rotation_deg = 12.0f;
        a.transform.scale = 1.3f;
        a.transform.offset_x = 0.05f;
        a.transform.opacity = 0.9f;
        a.masks.push_back(ClipMask{});                 // feathered ellipse
        Clip b = MakeGeneratedClip("bars-b", 0, 2.0, 1.0,
                                   SourceKind::ColorBars, "smpte75");
        b.effects.push_back({"builtin:xer.fx.vignette2",
                             {1.0f, 0.5f, 0.4f, 1.0f}});
        project.clips.push_back(b);
        Transition tr;
        tr.from_clip = "bars";
        tr.to_clip = "bars-b";
        tr.type = Transition::Type::Crossfade;
        tr.duration = 1.0;
        project.transitions.push_back(tr);
        Clip top = MakeGeneratedClip("top", 2, 0.0, 3.0,
                                     SourceKind::ColorBars, "smpte75");
        top.blend_mode = BlendMode::Multiply;
        top.transform.scale = 0.6f;
        project.clips.push_back(top);
        return project;
    };
    std::string err;
    const auto render = [&build](std::vector<std::uint8_t>* out,
                                 double* ms, std::string* err) {
        const Project project = build();
        Compositor compositor;
        if (!compositor.Init(project, {}, err)) return false;
        // Warmup: lazily builds LUTs / spawns pool workers so the timed
        // pass measures steady-state dispatch.
        if (!compositor.RenderFrameAt(1.8, out, err)) return false;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = compositor.RenderFrameAt(1.8, out, err);
        *ms = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - t0).count();
        compositor.Shutdown();
        return ok;
    };

    // Restores XER_VE_THREADS on every exit path (checks run in one
    // process — a leaked "1" would silently serialize every later
    // render check).
    const char* prev_env = std::getenv("XER_VE_THREADS");
    struct EnvGuard {
        ~EnvGuard() {
#if defined(_WIN32)
            // _putenv_s with an empty value deletes the variable.
            _putenv_s("XER_VE_THREADS", value.c_str());
#else
            if (value.empty()) unsetenv("XER_VE_THREADS");
            else setenv("XER_VE_THREADS", value.c_str(), 1);
#endif
        }
        std::string value;
    } guard{prev_env != nullptr ? prev_env : ""};
    const auto set_threads = [](const char* v) {
#if defined(_WIN32)
        _putenv_s("XER_VE_THREADS", v);
#else
        setenv("XER_VE_THREADS", v, 1);
#endif
    };

    std::vector<std::uint8_t> serial, pooled;
    double serial_ms = 0, pooled_ms = 0;
    set_threads("1");
    if (!render(&serial, &serial_ms, &err)) {
        return "serial render failed: " + err;
    }
    // Overshoot clamps to the pool's hardware bound (parallel_rows.cc).
    set_threads("64");
    if (!render(&pooled, &pooled_ms, &err)) {
        return "pooled render failed: " + err;
    }
    if (serial.size() != pooled.size() ||
        std::memcmp(serial.data(), pooled.data(), serial.size()) != 0) {
        return "parallel rows changed pixels: serial/pooled outputs "
               "differ";
    }
    std::fprintf(stderr,
                 "  parallel_determinism: serial %.1f ms  pooled "
                 "%.1f ms  bytes %zu identical\n",
                 serial_ms, pooled_ms, serial.size());
    return {};
}

// ---- l2_frame_cache ---------------------------------------------------
// Disk tier behind the RAM EffectFrameCache. Hermetic: the explicit
// ctor (no env gate) rooted under SmokeTempDir, a RAM capacity of
// exactly one frame, then store A -> store B (A evicts from RAM, both
// write-through to disk) -> Flush -> Lookup(A) must be an L2 hit that
// promotes the decoded .dxcfz bytes back into RAM, pixel-exact. Then
// a second Lookup(A) is a RAM hit (promotion works), and stats assert
// the counters moved the right way.
std::string CheckL2FrameCache() {
    const auto dir = SmokeTempDir() / "l2_frame_cache";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    constexpr int kW = 64, kH = 36;
    const std::size_t frame_bytes =
        static_cast<std::size_t>(kW) * kH * 4;
    auto l2 = std::make_shared<L2FrameCache>(
        dir, /*capacity_bytes*/ 64ull * 1024ull * 1024ull);
    EffectFrameCache cache(frame_bytes);   // exactly one frame fits
    cache.SetDiskTier(l2);

    const auto make_frame = [](std::uint8_t seed) {
        std::vector<std::uint8_t> px(kW * kH * 4);
        for (std::size_t i = 0; i < px.size(); ++i) {
            px[i] = static_cast<std::uint8_t>(seed + i * 31u);
        }
        video::Frame f;
        f.Adopt(kW, kH, video::PixelFormat::BGRA8, std::move(px));
        return f;
    };
    EffectFrameCache::Key ka{"clip-a", 1000, 1111};
    EffectFrameCache::Key kb{"clip-b", 2000, 2222};
    const video::Frame fa = make_frame(7), fb = make_frame(101);
    cache.Store(ka, fa);
    cache.Store(kb, fb);      // RAM capacity: 1 frame -> ka evicts
    l2->Flush();              // async writer drains to .dxcfz

    auto stats = cache.GetStats();
    if (stats.entries != 1 || stats.evictions != 1) {
        return "RAM tier did not evict as expected";
    }
    const auto l2st0 = l2->GetStats();
    if (l2st0.stores != 2) {
        return "write-through did not store 2 frames to disk";
    }

    video::Frame out;
    if (!cache.Lookup(ka, &out)) {
        return "L2 lookup miss for evicted frame";
    }
    if (out.Width() != kW || out.Height() != kH ||
        out.DataSize() != fa.DataSize() ||
        std::memcmp(out.Data(), fa.Data(), fa.DataSize()) != 0) {
        return "L2-promoted frame pixels differ";
    }
    stats = cache.GetStats();
    if (stats.l2_hits != 1 || stats.hits != 1) {
        return "L2 hit not counted in cache stats";
    }
    // The promote wrote ka back into RAM — a second lookup must be a
    // pure RAM hit (l2_hits stays 1) and must have evicted kb again.
    if (!cache.Lookup(ka, &out)) {
        return "promoted frame not served from RAM";
    }
    stats = cache.GetStats();
    if (stats.hits != 2 || stats.l2_hits != 1) {
        return "RAM re-hit after promote not counted";
    }
    // kb is still on disk even though it fell out of RAM again.
    if (!cache.Lookup(kb, &out) ||
        out.DataSize() != fb.DataSize() ||
        std::memcmp(out.Data(), fb.Data(), fb.DataSize()) != 0) {
        return "second frame did not survive RAM eviction via L2";
    }
    const auto l2st = l2->GetStats();
    std::fprintf(stderr,
                 "  l2_frame_cache: stores %llu hits %llu misses %llu "
                 "bytes_on_disk %llu\n",
                 (unsigned long long)l2st.stores,
                 (unsigned long long)l2st.hits,
                 (unsigned long long)l2st.misses,
                 (unsigned long long)l2st.bytes_on_disk);
    return {};
}

// ---- preview_perf ---------------------------------------------------
// End-to-end monitor-preview throughput harness — the check that
// answers "время рендера кадров". A deterministic fake decoder at the
// canvas size isolates the PIPELINE cost (decode ~0 ms) so the
// reported per-stage table splits render vs deliver honestly, and a
// manual FrameClock makes every tick land on a fresh frame — the
// measured number is pure render+deliver throughput, not wall-clock
// luck. Bound: a basic 640x360 timeline must present faster than
// realtime (>30 fps of engine work per frame).

// Deterministic decoder: endless WxH frames at a fixed fps. Each
// NextFrame writes a cheap moving gradient so the warp path sees
// realistic (non-uniform) pixels; decode time is counted so the
// report can separate "decode" from "composite" inside render_ms.
class PerfDecoder final : public IMediaDecoder {
public:
    PerfDecoder(int w, int h, double fps)
        : w_(w), h_(h), fps_(fps) {}
    bool Open(const std::string& utf8_path,
              std::string* /*error_out*/) override {
        opened_path_ = utf8_path;
        pos_ = 0.0;
        return true;
    }
    void Close() override {}
    bool IsOpen() const override { return !opened_path_.empty(); }
    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        const auto d0 = std::chrono::steady_clock::now();
        bgra->resize(static_cast<std::size_t>(w_) * h_ * 4u);
        // Moving 16px block pattern — compressible in cost terms but
        // not uniform, so resample/blend loops can't early-out.
        const int shift = static_cast<int>(pos_ * fps_) & 15;
        std::uint8_t* px = bgra->data();
        for (int y = 0; y < h_; ++y) {
            std::uint8_t* row = px + static_cast<std::size_t>(y) * w_ * 4u;
            for (int x = 0; x < w_; ++x) {
                const bool on = (((x + shift) >> 4) + (y >> 4)) & 1;
                row[x * 4 + 0] = on ? 200 : 30;
                row[x * 4 + 1] = on ? 160 : 30;
                row[x * 4 + 2] = on ? 90 : 30;
                row[x * 4 + 3] = 255;
            }
        }
        *width = w_;
        *height = h_;
        pos_ += 1.0 / fps_;
        decode_ns_.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - d0)
                .count()));
        decode_calls_.fetch_add(1);
        return true;
    }
    bool Seek(double seconds) override {
        pos_ = std::max(0.0, seconds);
        return true;
    }
    double GetPositionSeconds() const override { return pos_; }
    double GetFps() const override { return fps_; }
    double GetDurationSeconds() const override { return 60.0; }
    int GetWidth() const override { return w_; }
    int GetHeight() const override { return h_; }
    void EnableAudioTap(bool) override {}
    bool HasAudio() const override { return false; }
    double GetAudioStartSeconds() const override { return 0.0; }
    double GetAudioTimeBase() const override { return 0.0; }
    bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* /*out*/,
        int /*max_frames*/) override {
        return false;
    }
    bool AudioDrained() const override { return true; }
    std::uint64_t GetAudioEpoch() const override { return 0; }

    // Aggregate across every instance the factories hand out (the
    // compositor opens one per clip session).
    static std::atomic<std::uint64_t> decode_calls_;
    static std::atomic<std::uint64_t> decode_ns_;

private:
    int w_;
    int h_;
    double fps_;
    std::string opened_path_;
    double pos_ = 0.0;
};
std::atomic<std::uint64_t> PerfDecoder::decode_calls_{0};
std::atomic<std::uint64_t> PerfDecoder::decode_ns_{0};

// Virtual-time clock for the engine: NowSeconds only advances through
// SleepSeconds, so each playing tick lands exactly one project frame
// ahead regardless of how long the render actually took.
class PerfManualClock final : public FrameClock {
public:
    double NowSeconds() const override { return now_; }
    void SleepSeconds(double seconds) override {
        if (seconds > 0.0) {
            now_ += seconds;
        }
        // Let the harness thread observe progress without burning the
        // CPU the engine worker needs for the measurement.
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

private:
    double now_ = 0.0;
};

std::string CheckPreviewPerf() {
    constexpr int kW = 640;
    constexpr int kH = 360;
    constexpr int kFps = 30;
    constexpr int kWarmupFrames = 15;
    constexpr int kMeasureFrames = 60;
    Project p;
    p.width = kW;
    p.height = kH;
    p.fps_num = kFps;
    p.fps_den = 1;
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.tracks.push_back(Track{TrackKind::Audio, "A1"});
    Clip clip;
    clip.id = "perf.mp4";
    clip.track = 0;
    clip.timeline_start = 0.0;
    clip.duration = 30.0;
    clip.source.kind = SourceKind::Media;
    clip.source_path = "perf_synthetic.mp4";   // PerfDecoder ignores it
    p.clips.push_back(clip);

    auto factory = MediaDecoderFactory{[] {
        return MediaDecoderPtr(new PerfDecoder(kW, kH, kFps));
    }};

    // ---- stage A: engine end-to-end at full quality ----------------
    PreviewEngine::PerfStats full_stats;
    std::uint64_t presented = 0;
    {
        struct CountSink : PreviewEngine::Sink {
            void OnVideoFrame(
                Render::scoped_refptr<Render::VideoFrame> f) override {
                ++frames;
                last_w = f->visible_rect().width;
                last_h = f->visible_rect().height;
            }
            void OnAudioPeakLevels(float, float) override {}
            void OnPlayhead(double) override {}
            std::atomic<int> frames{0};
            std::atomic<int> last_w{0};
            std::atomic<int> last_h{0};
        } sink;
        PreviewEngine engine;
        PreviewOptions po;
        po.decoder_factory = factory;
        po.clock = std::make_shared<PerfManualClock>();
        po.audio_enabled = false;
        std::string err;
        if (!engine.Init(p, po, &sink, &err)) {
            return "perf engine init failed: " + err;
        }
        engine.SetPlaying(true);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (sink.frames.load() < kWarmupFrames + kMeasureFrames &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (sink.frames.load() < kWarmupFrames + kMeasureFrames) {
            return "perf playback stalled at " +
                   std::to_string(sink.frames.load()) + " frames";
        }
        // fx-free timeline: the autocache gate must leave the worker
        // off — no pre-render thread competing for the measured core.
        // WaitAutoCacheIdle returns trivially-true when disabled, so
        // this also flushes any settle delay if the gate regressed.
        engine.WaitAutoCacheIdle(10000);
        const AutoCacher::Stats ac = engine.AutoCacheStats();
        if (ac.done != 0 || ac.queued != 0) {
            return "autocache ran on an fx-free timeline (done=" +
                   std::to_string(ac.done) + ")";
        }
        engine.ResetPerfStats();
        const int warm_target = sink.frames.load() + kMeasureFrames;
        while (sink.frames.load() < warm_target &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        full_stats = engine.GetPerfStats();
        presented = static_cast<std::uint64_t>(sink.frames.load());
        if (sink.last_w.load() != kW || sink.last_h.load() != kH) {
            return "full-quality frame dims drifted";
        }

        // ---- stage B: quality tier 0.5 — canvas scaling --------------
        engine.ResetPerfStats();
        engine.SetQuality(0.5f);
        const int half_target = sink.frames.load() + kMeasureFrames;
        while (sink.frames.load() < half_target &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const PreviewEngine::PerfStats half_stats =
            engine.GetPerfStats();
        const int exp_w = (kW + 1) / 2;
        const int exp_h = kH / 2;
        if (sink.last_w.load() != exp_w || sink.last_h.load() != exp_h) {
            return "half-quality frame dims wrong: got " +
                   std::to_string(sink.last_w.load()) + "x" +
                   std::to_string(sink.last_h.load()) + " want " +
                   std::to_string(exp_w) + "x" + std::to_string(exp_h);
        }
        engine.Shutdown();

        const auto avg = [](double ms, std::uint64_t n) {
            return n == 0 ? 0.0 : ms / static_cast<double>(n);
        };
        const double decode_ms =
            static_cast<double>(PerfDecoder::decode_ns_.load()) / 1e6;
        const std::uint64_t decode_n = PerfDecoder::decode_calls_.load();
        const double full_frame = avg(
            full_stats.render_ms + full_stats.deliver_ms,
            full_stats.renders);
        const double half_frame = avg(
            half_stats.render_ms + half_stats.deliver_ms,
            half_stats.renders);
        std::fprintf(
            stderr,
            "  preview_perf: %llux%llu@%dfps media clip, %llu frames\n"
            "    full q: render %.2f ms (decode %.2f ms inside) "
            "deliver %.2f ms audio %.2f ms -> %.1f fps\n"
            "    half q: render %.2f ms deliver %.2f ms -> %.1f fps "
            "(%.0f%% of full-res cost)\n",
            (unsigned long long)kW, (unsigned long long)kH, kFps,
            (unsigned long long)presented,
            avg(full_stats.render_ms, full_stats.renders),
            avg(decode_ms, decode_n),
            avg(full_stats.deliver_ms, full_stats.renders),
            avg(full_stats.audio_ms, full_stats.ticks),
            full_frame > 0.0 ? 1000.0 / full_frame : 0.0,
            avg(half_stats.render_ms, half_stats.renders),
            avg(half_stats.deliver_ms, half_stats.renders),
            half_frame > 0.0 ? 1000.0 / half_frame : 0.0,
            full_frame > 0.0 ? 100.0 * half_frame / full_frame : 0.0);
        // The bound: >30fps of render+deliver on a basic timeline.
        // (Half-quality is a report only — no bound, it must simply
        // be faster and correctly sized.)
        if (full_frame >= 1000.0 / 30.0) {
            return "basic 640x360 timeline below realtime: " +
                   std::to_string(full_frame) + " ms/frame";
        }
        if (half_stats.renders == 0) {
            return "half-quality tier produced no frames";
        }
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

// The retired xer_fx_* / xer_audio_fx plugin kernels now live
// host-side (builtin_fx.cc, registered into PluginHost). Every
// previously-available "plugin:<id>" (and the new "builtin:<id>"
// alias) must still resolve, and each effect must apply cleanly
// through the shared ABI — video effects onto a small BGRA frame,
// audio effects onto a short PCM buffer.
std::string CheckBuiltinFx() {
    const std::vector<std::string> ids = BuiltinFx::Ids();
    // 49 migrated effects + xer.fx.smart_reframe + xer.fx.track_mosaic
    // + xer.fx.face_blur + xer.fx.track_mask (all four geometric/
    // track-driven — their ABI fns are identities; the compositor
    // applies them at the warp/mask pass) + the film-look kit
    // (xer.fx.halation/gate_weave/light_leak/film_flicker/
    // dust_scratch/vignette2 — film_grain was already migrated and
    // is upgraded in place, not added) + the 14 effects_rack
    // exposures (xer.af.compressor/limiter/gate/expander,
    // eq_parametric/eq_graphic, chorus/flanger/phaser/vibrato/
    // ring_mod, autotune, reverb + reverb_ir).
    if (ids.size() != 73) {
        return "builtin table size drifted from the migrated set";
    }
    for (const std::string& id : ids) {
        if (PluginHost::ForEffectType("plugin:" + id) == nullptr) {
            return "plugin:" + id + " no longer resolves";
        }
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:" + id);
        if (desc == nullptr || !desc->builtin) {
            return "builtin:" + id + " does not resolve to a builtin";
        }
    }
    // Apply every effect once. A 16x16 gradient frame keeps the heavy
    // kernels cheap; each fn must report success and the frame must
    // stay BGRA-valid (the apply itself is in place).
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(16 * 16 * 4));
    for (std::size_t i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<std::uint8_t>((i * 37 + 11) & 0xFF);
    }
    for (const std::string& id : ids) {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:" + id);
        if (desc->kind == XER_PLUGIN_VIDEO_FX) {
            if (!PluginHost::ApplyVideo(*desc, frame.data(), 16, 16,
                                        16 * 4,
                                        {0.5f, 0.5f, 0.5f, 0.5f})) {
                return id + " builtin apply failed";
            }
        }
    }
    // A mutating spot check: threshold_bw must binarize the gradient.
    {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:xer.fx.threshold_bw");
        if (desc == nullptr ||
            !PluginHost::ApplyVideo(*desc, frame.data(), 16, 16, 16 * 4,
                                    {128.f})) {
            return "xer.fx.threshold_bw apply failed";
        }
        for (std::size_t i = 0; i + 3 < frame.size(); i += 4) {
            if (frame[i] != 0 && frame[i] != 255) {
                return "threshold_bw left a mid-tone pixel";
            }
            if (frame[i] != frame[i + 1] || frame[i] != frame[i + 2]) {
                return "threshold_bw produced non-gray output";
            }
        }
    }
    // Audio: every xer.af.* applies to a stereo buffer at 48 kHz.
    std::vector<float> pcm(static_cast<std::size_t>(512 * 2));
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = 0.25f * std::sin(static_cast<float>(i) * 0.05f);
    }
    for (const std::string& id : ids) {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:" + id);
        if (desc->kind == XER_PLUGIN_AUDIO_FX) {
            // xer.af.voice_isolate needs the staged GTCRN model + a
            // loadable ONNX backend — the gated block below exercises
            // it (or skips it) explicitly.
            if (id == "xer.af.voice_isolate") continue;
            std::vector<float> copy = pcm;
            if (!PluginHost::ApplyAudio(*desc, copy.data(), 512, 2,
                                        48000, {})) {
                return id + " builtin apply failed";
            }
        }
    }
    // xer.af.voice_isolate (GTCRN ONNX): when the model + an ONNX
    // backend are staged, a noise-only region must come back clearly
    // quieter while a speech-like harmonic region keeps energy —
    // the same contract Premiere's "Enhance Speech" implies. When the
    // environment cannot run it, the apply is allowed to fail but the
    // graceful-degradation contract still holds: no crash, failure
    // with a reason (verified via ProcessBuffer's error path).
    {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:xer.af.voice_isolate");
        if (desc == nullptr || desc->audio_fn == nullptr) {
            return "xer.af.voice_isolate missing an audio kernel";
        }
        if (!VoiceIsolate::Available()) {
            std::fprintf(stdout,
                         "  builtin_fx voice_isolate: SKIP (model or "
                         "ONNX backend unavailable)\n");
            std::fflush(stdout);
            // The kernel must fail (not crash / not silently pass)
            // on a real buffer without its runtime.
            std::vector<float> copy = pcm;
            PluginHost::ApplyAudio(*desc, copy.data(), 512, 2, 48000,
                                   {});
            return {};
        }
        // 1 s @ 48 kHz mono: first half = stationary noise only,
        // second half = a speech-like harmonic stack (150 Hz + 7
        // partials, syllable-ish 4 Hz AM) over the same noise.
        const int n = 48000;
        std::vector<float> noisy(static_cast<std::size_t>(n));
        std::uint32_t rng = 0x9E3779B9u;
        auto noise = [&] {
            rng = rng * 1664525u + 1013904223u;
            return (static_cast<float>((rng >> 9) & 0x7FFFFF) /
                        4194304.0f -
                    1.0f) *
                   0.15f;
        };
        for (int i = 0; i < n; ++i) {
            float s = noise();
            if (i >= n / 2) {
                const float t = static_cast<float>(i) / 48000.0f;
                float v = 0.0f;
                for (int k = 1; k <= 8; ++k) {
                    v += (0.3f / k) *
                         std::sin(2.0f * 3.14159265f * 150.0f * k * t);
                }
                s += v * (0.6f +
                          0.4f * std::sin(2.0f * 3.14159265f * 4.0f *
                                          t));
            }
            noisy[i] = s;
        }
        const auto rms = [](const std::vector<float>& v, int a,
                            int b) {
            double acc = 0.0;
            for (int i = a; i < b; ++i) acc += v[i] * v[i];
            return std::sqrt(acc / (b - a));
        };
        const double noise_before = rms(noisy, 4000, 20000);
        const double sig_before = rms(noisy, 26000, 46000);
        std::vector<float> cleaned = noisy;
        if (!PluginHost::ApplyAudio(*desc, cleaned.data(), n, 1, 48000,
                                    {1.0f})) {
            return "xer.af.voice_isolate apply failed despite staged "
                   "model + backend";
        }
        const double noise_after = rms(cleaned, 4000, 20000);
        const double sig_after = rms(cleaned, 26000, 46000);
        std::fprintf(stdout,
                     "  builtin_fx voice_isolate: noise %.4f->%.4f, "
                     "signal %.4f->%.4f\n",
                     noise_before, noise_after, sig_before, sig_after);
        std::fflush(stdout);
        for (float s : cleaned) {
            if (!std::isfinite(s)) {
                return "voice_isolate produced non-finite output";
            }
        }
        if (noise_after >= noise_before * 0.7) {
            return "voice_isolate did not reduce the noise region";
        }
        if (sig_after <= sig_before * 0.3) {
            return "voice_isolate destroyed the harmonic region";
        }
    }
    return {};
}

// FFmpeg xfade transitions (docs/wiki/research/2026-11-24 §1.2):
// synthetic A/B frames through the cached-graph bridge plus the
// compositor's Type::Xfade dispatch. Endpoints must pin to A/B,
// a true wipe must differ from alpha crossfade at mid-progress,
// xfade "fade" must match our crossfade math (±1 LSB of rounding),
// and the unknown-name contract must fall back to crossfade.
std::string CheckXfade() {
    if (!XfadeAvailable()) {
        return "xfade filter missing from the linked libavfilter";
    }
    constexpr int kW = 64, kH = 48;
    // A = solid opaque red; B = opaque blue with a horizontal
    // gradient so both a uniform blend and a spatial wipe are
    // distinguishable.
    video::Frame A, B;
    if (!A.Allocate(kW, kH, video::PixelFormat::BGRA8) ||
        !B.Allocate(kW, kH, video::PixelFormat::BGRA8)) {
        return "xfade test frames failed to allocate";
    }
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            std::uint8_t* a =
                A.Data() + (static_cast<size_t>(y) * kW + x) * 4;
            std::uint8_t* b =
                B.Data() + (static_cast<size_t>(y) * kW + x) * 4;
            a[0] = 0;   a[1] = 0;   a[2] = 255; a[3] = 255;
            b[0] = static_cast<std::uint8_t>(64 + x * 2);
            b[1] = 32;  b[2] = 0;   b[3] = 255;
        }
    }
    const auto same = [](const video::Frame& x, const video::Frame& y) {
        return x.DataSize() == y.DataSize() &&
               std::memcmp(x.Data(), y.Data(), x.DataSize()) == 0;
    };
    const auto max_diff = [](const video::Frame& x,
                             const video::Frame& y) {
        int worst = 0;
        const size_t n = std::min(x.DataSize(), y.DataSize());
        for (size_t i = 0; i < n; ++i) {
            worst = std::max(worst, std::abs(
                static_cast<int>(x.Data()[i]) -
                static_cast<int>(y.Data()[i])));
        }
        return worst;
    };
    // The exported type table must be populated and probeable.
    if (XfadeTransitions().empty() ||
        !XfadeTypeAvailable("circleopen") ||
        XfadeTypeAvailable("not_a_real_xfade")) {
        return "xfade type table / availability probe is wrong";
    }
    // Endpoints: w=0 must be exactly A, w=1 exactly B.
    video::Frame out;
    if (!ApplyXfadeTransition("circleopen", 0.0, A, B, &out) ||
        !same(out, A)) {
        return "xfade w=0 did not reproduce the outgoing frame";
    }
    if (!ApplyXfadeTransition("circleopen", 1.0, A, B, &out) ||
        !same(out, B)) {
        return "xfade w=1 did not reproduce the incoming frame";
    }
    // Our plain crossfade at w=0.5 as the blend reference.
    video::Frame xf;
    if (!ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                               A, B, &xf, "")) {
        return "crossfade reference eval failed";
    }
    // Wipes must differ from a uniform blend at mid-progress;
    // xfade's own "fade" must match it (same linear math, allow
    // 1 LSB of pixel-format rounding inside the filter graph).
    const char* kWipes[] = {"radial", "circleopen", "coverleft",
                            "pixelize"};
    for (const char* name : kWipes) {
        if (!ApplyXfadeTransition(name, 0.5, A, B, &out)) {
            return std::string("xfade '") + name + "' eval failed";
        }
        if (same(out, xf)) {
            return std::string("xfade '") + name +
                "' is indistinguishable from crossfade at w=0.5";
        }
    }
    if (!ApplyXfadeTransition("fade", 0.5, A, B, &out) ||
        max_diff(out, xf) > 1) {
        return "xfade 'fade' diverged from crossfade at w=0.5";
    }
    // Compositor dispatch: Type::Xfade + name must equal the bridge
    // result; an unknown name must fall back to crossfade.
    video::Frame via_comp;
    if (!ApplyTransitionPixels(Transition::Type::Xfade, 0.5,
                               A, B, &via_comp, "circleopen")) {
        return "Type::Xfade compositor dispatch failed";
    }
    video::Frame circle;
    if (!ApplyXfadeTransition("circleopen", 0.5, A, B, &circle) ||
        !same(via_comp, circle)) {
        return "Type::Xfade dispatch diverged from the bridge";
    }
    if (!ApplyTransitionPixels(Transition::Type::Xfade, 0.5,
                               A, B, &via_comp,
                               "not_a_real_xfade") ||
        !same(via_comp, xf)) {
        return "unknown xfade name did not fall back to crossfade";
    }
    // Cache-exercise: sweep progress on one (name,geometry) graph —
    // must stay sane across repeated evals, then drop the cache.
    for (double w = 0.1; w < 1.0; w += 0.2) {
        if (!ApplyXfadeTransition("wipetl", w, A, B, &out)) {
            return "xfade progress sweep failed mid-graph";
        }
    }
    XfadeGraphCacheClear();
    return {};
}

// XER-native xer.tr.* transitions (docs/wiki/research/2026-11-24-
// transitions.md): synthetic A/B frames through every registered
// kernel — endpoints must pin exactly to A/B (dispatcher memcpy
// short-circuit), mid-progress must be valid AND distinct from a
// plain crossfade (a transition that degenerates to the reference
// blend has no reason to exist), evals must be deterministic
// (same inputs -> identical output), the param schema must
// parse/format round-trip, and an unknown id must fail at the lib
// boundary while the compositor dispatch falls back to crossfade.
std::string CheckXerTransitions() {
    constexpr int kW = 64, kH = 48;
    video::Frame A, B;
    if (!A.Allocate(kW, kH, video::PixelFormat::BGRA8) ||
        !B.Allocate(kW, kH, video::PixelFormat::BGRA8)) {
        return "xer.tr test frames failed to allocate";
    }
    // A = warm left/right split; B = cool gradient — both spatial and
    // color differences so reveal/blend kernels are distinguishable.
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            std::uint8_t* a =
                A.Data() + (static_cast<size_t>(y) * kW + x) * 4;
            std::uint8_t* b =
                B.Data() + (static_cast<size_t>(y) * kW + x) * 4;
            const bool left = x < kW / 2;
            a[0] = 20;  a[1] = 40;  a[2] = left ? 235 : 60;
            a[3] = 255;
            b[0] = static_cast<std::uint8_t>(60 + x * 2);
            b[1] = static_cast<std::uint8_t>(40 + y * 3);
            b[2] = 30;
            b[3] = 255;
        }
    }
    const auto same = [](const video::Frame& x, const video::Frame& y) {
        return x.DataSize() == y.DataSize() &&
               std::memcmp(x.Data(), y.Data(), x.DataSize()) == 0;
    };
    // The registry must be populated and probeable.
    if (XerTransitions().size() < 12 ||
        !XerTransitionKnown("xer.tr.whip_pan") ||
        XerTransitionKnown("xer.tr.not_real")) {
        return "xer.tr registry / probe is wrong";
    }
    // Param schema: parse honors overrides + clamps, format emits
    // only non-defaults, round-trips.
    {
        const XerTransitionParams p =
            ParseXerTransitionParams("dir=3;soft=0.4;int=0.5;seed=9");
        if (p.direction != 3 || std::fabs(p.softness - 0.4) > 1e-9 ||
            std::fabs(p.intensity - 0.5) > 1e-9 || p.seed != 9) {
            return "xer.tr params did not parse";
        }
        const XerTransitionParams clamped =
            ParseXerTransitionParams("soft=9;int=-2;bogus=1");
        if (clamped.softness != 1.0 || clamped.intensity != 0.0) {
            return "xer.tr params did not clamp";
        }
        const XerTransitionParams round =
            ParseXerTransitionParams(FormatXerTransitionParams(p));
        if (round.direction != p.direction ||
            std::fabs(round.softness - p.softness) > 1e-9 ||
            std::fabs(round.intensity - p.intensity) > 1e-9 ||
            round.seed != p.seed) {
            return "xer.tr params format/parse did not round-trip";
        }
        if (!FormatXerTransitionParams(XerTransitionParams{}).empty()) {
            return "xer.tr default params should serialize empty";
        }
    }
    // Crossfade reference for distinctness checks.
    video::Frame xf;
    if (!ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                               A, B, &xf, "")) {
        return "crossfade reference eval failed";
    }
    // Every registered id: endpoints exact, mid valid + distinct
    // from crossfade, deterministic across two evals.
    video::Frame out, out2;
    for (const XerTransitionInfo& info : XerTransitions()) {
        const std::string id = info.id;
        if (!ApplyXerTransition(id, XerTransitionParams{}, 0.0,
                                A, B, &out) ||
            !same(out, A)) {
            return id + ": w=0 did not reproduce the outgoing frame";
        }
        if (!ApplyXerTransition(id, XerTransitionParams{}, 1.0,
                                A, B, &out) ||
            !same(out, B)) {
            return id + ": w=1 did not reproduce the incoming frame";
        }
        bool any_mid_content = false;
        for (double w : {0.25, 0.5, 0.75}) {
            if (!ApplyXerTransition(id, XerTransitionParams{}, w,
                                    A, B, &out)) {
                return id + ": mid-progress eval failed";
            }
            bool all_zero = true, all_255 = true;
            for (size_t i = 0; i < out.DataSize(); ++i) {
                if (out.Data()[i] != 0) all_zero = false;
                if (out.Data()[i] != 255) all_255 = false;
            }
            // Kernels whose mid point is a legit full-field state
            // (flash blows out to white at w=0.5) still have to carry
            // real content somewhere inside the window.
            if (!all_zero && !all_255) {
                any_mid_content = true;
            }
        }
        if (!any_mid_content) {
            return id + ": degenerate at every mid-progress point";
        }
        if (!ApplyXerTransition(id, XerTransitionParams{}, 0.5,
                                A, B, &out)) {
            return id + ": w=0.5 eval failed";
        }
        if (same(out, xf)) {
            return id + ": indistinguishable from crossfade at w=0.5";
        }
        if (!ApplyXerTransition(id, XerTransitionParams{}, 0.5,
                                A, B, &out2) ||
            !same(out, out2)) {
            return id + ": not deterministic across evals";
        }
        // Non-default params must still evaluate (and for seeded
        // kernels a different seed is a different frame).
        XerTransitionParams alt;
        alt.direction = 2;
        alt.softness = 0.5;
        alt.intensity = 0.6;
        alt.seed = 12345;
        if (!ApplyXerTransition(id, alt, 0.5, A, B, &out2)) {
            return id + ": non-default params eval failed";
        }
    }
    // Unknown id: lib returns false; the compositor dispatch falls
    // back to a true crossfade (same contract as a bad xfade token).
    if (ApplyXerTransition("xer.tr.not_real", XerTransitionParams{},
                          0.5, A, B, &out)) {
        return "unknown xer.tr id did not fail at the lib boundary";
    }
    if (!ApplyTransitionPixels(Transition::Type::XerFx, 0.5,
                               A, B, &out, "", "xer.tr.whip_pan",
                               "")) {
        return "Type::XerFx compositor dispatch failed";
    }
    video::Frame via_lib;
    if (!ApplyXerTransition("xer.tr.whip_pan", XerTransitionParams{},
                            0.5, A, B, &via_lib) ||
        !same(out, via_lib)) {
        return "Type::XerFx dispatch diverged from the lib";
    }
    if (!ApplyTransitionPixels(Transition::Type::XerFx, 0.5,
                               A, B, &out, "", "xer.tr.not_real",
                               "") ||
        !same(out, xf)) {
        return "unknown xer.tr id did not fall back to crossfade";
    }
    // Params reach the kernel through the compositor: seeded glitch
    // with two seeds must differ.
    video::Frame g1, g2;
    if (!ApplyTransitionPixels(Transition::Type::XerFx, 0.5,
                               A, B, &g1, "", "xer.tr.glitch",
                               "seed=1") ||
        !ApplyTransitionPixels(Transition::Type::XerFx, 0.5,
                               A, B, &g2, "", "xer.tr.glitch",
                               "seed=2") ||
        same(g1, g2)) {
        return "xer.tr params did not reach the kernel";
    }
    // Wire-name round trip covers the new enum.
    {
        Transition::Type back = Transition::Type::Crossfade;
        if (!TransitionTypeFromString(
                TransitionTypeToString(Transition::Type::XerFx),
                &back) ||
            back != Transition::Type::XerFx) {
            return "XerFx wire-name round trip failed";
        }
    }
    return {};
}

// The 14 effects_rack exposures (xer.af.* added in wave-1 §1.2b):
// every descriptor must resolve + apply on stereo 48 kHz PCM with
// both default and out-of-range params (params must clamp, never
// crash or return NaN), and the dynamics/space kernels get a real
// semantic assert — limiter ceiling, gate muting, compressor
// gain reduction, and the convolution reverb's tail.
std::string CheckAudioFxRack() {
    const char* kIds[] = {
        "xer.af.compressor", "xer.af.limiter",
        "xer.af.gate",       "xer.af.expander",
        "xer.af.eq_parametric", "xer.af.eq_graphic",
        "xer.af.chorus",     "xer.af.flanger",
        "xer.af.phaser",     "xer.af.vibrato",
        "xer.af.ring_mod",   "xer.af.autotune",
        "xer.af.reverb",     "xer.af.reverb_ir",
    };
    constexpr int kFrames = 4096, kCh = 2, kSr = 48000;
    const auto make_pcm = [&](double amp, double hz, int frames) {
        std::vector<float> v(static_cast<size_t>(frames) * kCh);
        for (int i = 0; i < frames; ++i) {
            const float s = static_cast<float>(
                amp * std::sin(2.0 * 3.14159265 * hz * i / kSr));
            for (int c = 0; c < kCh; ++c) {
                v[static_cast<size_t>(i) * kCh + c] = s;
            }
        }
        return v;
    };
    const auto finite = [](const std::vector<float>& v) {
        for (float s : v) {
            if (!std::isfinite(s)) return false;
        }
        return true;
    };
    const auto peak = [](const std::vector<float>& v) {
        float p = 0.f;
        for (float s : v) p = std::max(p, std::fabs(s));
        return p;
    };
    const auto rms = [](const std::vector<float>& v) {
        double acc = 0.0;
        for (float s : v) acc += static_cast<double>(s) * s;
        return std::sqrt(acc / static_cast<double>(v.size()));
    };
    const auto apply = [&](const char* id, std::vector<float>* pcm,
                           const std::vector<float>& params) {
        const PluginDesc* d =
            PluginHost::ForEffectType(std::string("builtin:") + id);
        if (d == nullptr || !d->builtin || d->audio_fn == nullptr) {
            return false;
        }
        return PluginHost::ApplyAudio(
            *d, pcm->data(),
            static_cast<int32_t>(pcm->size() / kCh), kCh, kSr,
            params);
    };
    // Resolve + apply: defaults, then deliberately wild params.
    for (const char* id : kIds) {
        std::vector<float> pcm = make_pcm(0.5, 440.0, kFrames);
        if (!apply(id, &pcm, {})) {
            return std::string(id) + " apply failed on defaults";
        }
        if (!finite(pcm)) {
            return std::string(id) + " produced non-finite output";
        }
        std::vector<float> wild = make_pcm(0.5, 440.0, kFrames);
        const PluginDesc* d =
            PluginHost::ForEffectType(std::string("builtin:") + id);
        std::vector<float> params(d->params.size(), 1.0e9f);
        if (!apply(id, &wild, params)) {
            return std::string(id) +
                " rejected out-of-range params (must clamp)";
        }
        if (!finite(wild)) {
            return std::string(id) +
                " produced non-finite output on wild params";
        }
    }
    // Limiter: ceiling -20 dB on a constant 0.95 DC input. A sine
    // would legitimately recover gain between peaks (the release
    // runs per-sample through the troughs), so DC is the honest
    // probe — gain converges to ceiling/0.95 ≈ 0.105 and the tail
    // of the block must sit at ~0.1.
    {
        std::vector<float> pcm(static_cast<size_t>(kFrames) * kCh,
                               0.95f);
        if (!apply("xer.af.limiter", &pcm, {-20.f, 5.f})) {
            return "xer.af.limiter apply failed";
        }
        float tail_peak = 0.f;
        for (size_t i = pcm.size() / 2; i < pcm.size(); ++i) {
            tail_peak = std::max(tail_peak, std::fabs(pcm[i]));
        }
        if (tail_peak > 0.13f) {
            return "xer.af.limiter overshot its ceiling";
        }
        if (tail_peak < 0.05f) {
            return "xer.af.limiter crushed the signal below ceiling";
        }
    }
    // Gate: -70 dBFS noise floor under a -40 dB threshold must come
    // back (near-)silent.
    {
        std::vector<float> pcm(static_cast<size_t>(kFrames) * kCh);
        std::uint32_t rng = 0x51ED1E55u;
        for (float& s : pcm) {
            rng = rng * 1664525u + 1013904223u;
            s = (static_cast<float>((rng >> 9) & 0x7FFFFF) /
                     4194304.f - 1.f) * 0.0004f;
        }
        const double in = rms(pcm);
        if (!apply("xer.af.gate", &pcm, {-40.f, 10.f})) {
            return "xer.af.gate apply failed";
        }
        if (rms(pcm) > in * 0.2) {
            return "xer.af.gate did not mute below-threshold audio";
        }
    }
    // Compressor: -20 dB threshold / 8:1 on a 0.9 sine — steady-state
    // gain reduction must pull the tail of the block well under the
    // input peak (early attack overshoot is allowed, so compare the
    // last half).
    {
        std::vector<float> pcm = make_pcm(0.9, 440.0, kFrames);
        if (!apply("xer.af.compressor", &pcm,
                   {-20.f, 8.f, 5.f, 80.f})) {
            return "xer.af.compressor apply failed";
        }
        float tail_peak = 0.f;
        for (size_t i = pcm.size() / 2; i < pcm.size(); ++i) {
            tail_peak = std::max(tail_peak, std::fabs(pcm[i]));
        }
        if (tail_peak > 0.45f || tail_peak < 0.02f) {
            return "xer.af.compressor did not reduce gain in range";
        }
    }
    // Convolution reverb: an impulse must grow a decaying tail.
    {
        std::vector<float> pcm(static_cast<size_t>(kFrames) * kCh,
                               0.f);
        pcm[0] = pcm[1] = 1.f;
        if (!apply("xer.af.reverb_ir", &pcm, {1.0f, 0.4f, 1.0f})) {
            return "xer.af.reverb_ir apply failed";
        }
        double tail = 0.0;
        for (int i = 200; i < kFrames; ++i) {
            for (int c = 0; c < kCh; ++c) {
                const float s = pcm[static_cast<size_t>(i) * kCh + c];
                tail += s * s;
            }
        }
        if (tail < 1.0e-6) {
            return "xer.af.reverb_ir produced no impulse tail";
        }
    }
    return {};
}

// The film-look kit (docs/wiki/research/2026-11-24 §1.1): film_grain
// v2, halation, gate_weave, light_leak, film_flicker, dust_scratch
// and vignette2. The time-aware ids dispatch through the compositor
// with (media_time, clip_seed) — identical inputs must bit-match
// while the output animates across time. Each kernel's signature is
// verified on synthetic BGRA frames, plus one generated-source
// project through the real compositor path.
std::string CheckFilmFx() {
    namespace BF = BuiltinFx;
    constexpr int kW = 64, kH = 36, kStride = kW * 4;
    const char* kAll[] = {
        "xer.fx.film_grain",   "xer.fx.halation",
        "xer.fx.gate_weave",   "xer.fx.light_leak",
        "xer.fx.film_flicker", "xer.fx.dust_scratch",
        "xer.fx.vignette2",
    };
    for (const char* id : kAll) {
        const PluginDesc* d =
            PluginHost::ForEffectType(std::string("builtin:") + id);
        if (d == nullptr || !d->builtin || d->video_fn == nullptr) {
            return std::string("builtin:") + id + " is not registered";
        }
    }
    const char* kTimed[] = {
        "xer.fx.film_grain", "xer.fx.gate_weave", "xer.fx.light_leak",
        "xer.fx.film_flicker", "xer.fx.dust_scratch",
    };
    for (const char* id : kTimed) {
        if (!BF::IsFilmEffectId(id)) {
            return std::string(id) + " missing from the film set";
        }
    }
    // halation/vignette2 are pure per-frame kernels — the normal ABI
    // path applies them; they must NOT sit in the time-aware set.
    if (BF::IsFilmEffectId("xer.fx.halation") ||
        BF::IsFilmEffectId("xer.fx.vignette2")) {
        return "a pure per-frame kernel leaked into the film set";
    }

    const auto flat = [](std::uint8_t v) {
        std::vector<std::uint8_t> f(static_cast<size_t>(kW) * kH * 4);
        for (std::size_t i = 0; i + 3 < f.size(); i += 4) {
            f[i] = f[i + 1] = f[i + 2] = v;
            f[i + 3] = 255;
        }
        return f;
    };
    const auto film = [](const char* id, std::vector<std::uint8_t>* f,
                         const std::vector<float>& params, double t,
                         std::uint64_t seed) {
        return BF::ApplyFilmEffect(id, f->data(), kW, kH, kStride,
                                   params, t, seed);
    };
    const auto variance = [](const std::vector<std::uint8_t>& f) {
        const double npx = static_cast<double>(f.size()) / 4.0;
        double mean = 0.0;
        for (std::size_t i = 0; i < f.size(); i += 4) mean += f[i];
        mean /= npx;
        double var = 0.0;
        for (std::size_t i = 0; i < f.size(); i += 4) {
            const double d = f[i] - mean;
            var += d * d;
        }
        return var / npx;
    };
    const auto mean_luma = [](const std::vector<std::uint8_t>& f) {
        double m = 0.0;
        for (std::size_t i = 0; i < f.size(); i += 4) {
            m += 0.114 * f[i] + 0.587 * f[i + 1] + 0.299 * f[i + 2];
        }
        return m / (static_cast<double>(f.size()) / 4.0);
    };

    // film_grain: deterministic at fixed (seed,time), adds variance
    // to a flat field, regenerates across time, shadow-weighted.
    {
        std::vector<std::uint8_t> a = flat(90), b = flat(90);
        if (!film("xer.fx.film_grain", &a, {40.f, 1.f, 2.f, 7.f}, 0.5,
                  42) ||
            !film("xer.fx.film_grain", &b, {40.f, 1.f, 2.f, 7.f}, 0.5,
                  42)) {
            return "film_grain apply failed";
        }
        if (a != b) {
            return "film_grain is not deterministic at fixed time";
        }
        if (variance(a) < 4.0) {
            return "film_grain added no measurable variance";
        }
        std::vector<std::uint8_t> c = flat(90);
        film("xer.fx.film_grain", &c, {40.f, 1.f, 2.f, 7.f}, 0.6, 42);
        if (a == c) {
            return "film_grain did not regenerate across time";
        }
        std::vector<std::uint8_t> dark = flat(40), bright = flat(215);
        film("xer.fx.film_grain", &dark, {60.f, 1.f, 1.f, 3.f}, 0.5,
             42);
        film("xer.fx.film_grain", &bright, {60.f, 1.f, 1.f, 3.f}, 0.5,
             42);
        if (variance(dark) <= variance(bright) * 2.0) {
            return "film_grain is not shadow-weighted";
        }
    }

    // halation (pure ABI): the bright core must bleed warm light
    // outward — pixels beside it gain red over blue — while the far
    // corner stays dark.
    {
        std::vector<std::uint8_t> f = flat(0);
        for (int y = 14; y < 22; ++y) {
            for (int x = 28; x < 36; ++x) {
                std::uint8_t* px =
                    f.data() + (static_cast<size_t>(y) * kW + x) * 4;
                px[0] = px[1] = px[2] = 255;
            }
        }
        const PluginDesc* d =
            PluginHost::ForEffectType("builtin:xer.fx.halation");
        if (!PluginHost::ApplyVideo(*d, f.data(), kW, kH, kStride,
                                    {200.f, 10.f, 0.9f, 25.f})) {
            return "halation apply failed";
        }
        const std::uint8_t* near_px =
            f.data() + (static_cast<size_t>(18) * kW + 22) * 4;
        const std::uint8_t* far_px =
            f.data() + (static_cast<size_t>(2) * kW + 2) * 4;
        if (near_px[2] <= near_px[0] || near_px[2] < 8) {
            return "halation did not bleed warm light";
        }
        if (far_px[2] >= near_px[2]) {
            return "halation bloomed the shadows";
        }
    }

    // gate_weave: same-time bit-match plus a measurable translation
    // somewhere along the seeded wander curve.
    {
        std::vector<std::uint8_t> f = flat(0);
        for (int y = 0; y < kH; ++y) {
            std::uint8_t* px =
                f.data() + (static_cast<size_t>(y) * kW + 32) * 4;
            px[0] = px[1] = px[2] = 255;
        }
        std::vector<std::uint8_t> a = f, b = f;
        film("xer.fx.gate_weave", &a, {4.f, 1.0f, 3.f}, 0.5, 9);
        film("xer.fx.gate_weave", &b, {4.f, 1.0f, 3.f}, 0.5, 9);
        if (a != b) {
            return "gate_weave is not deterministic at fixed time";
        }
        double best = 0.0;
        for (double t = 0.05; t < 3.0; t += 0.1) {
            std::vector<std::uint8_t> g = f;
            film("xer.fx.gate_weave", &g, {4.f, 1.0f, 3.f}, t, 9);
            double cx = 0.0, mass = 0.0;
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const double v =
                        g[(static_cast<size_t>(y) * kW + x) * 4 + 2];
                    cx += v * x;
                    mass += v;
                }
            }
            if (mass > 0.0) {
                best = std::max(best, std::fabs(cx / mass - 32.0));
            }
        }
        if (best < 0.5) {
            return "gate_weave never displaced the frame";
        }
    }

    // light_leak: the edge band must warm up more than the center.
    {
        std::vector<std::uint8_t> g = flat(60), g2 = flat(60);
        film("xer.fx.light_leak", &g, {1.0f, 30.f, 5.f, 0.f}, 0.5, 77);
        film("xer.fx.light_leak", &g2, {1.0f, 30.f, 5.f, 0.f}, 0.5, 77);
        if (g != g2) {
            return "light_leak is not deterministic at fixed time";
        }
        if (g == flat(60)) {
            return "light_leak changed nothing";
        }
        const auto warm_delta = [&](bool edge_band) {
            double sum = 0.0;
            int cnt = 0;
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const bool edge = x < 6 || x >= kW - 6 || y < 6 ||
                                      y >= kH - 6;
                    if (edge != edge_band) continue;
                    const std::size_t i =
                        (static_cast<size_t>(y) * kW + x) * 4;
                    sum += static_cast<int>(g[i + 2]) -
                           static_cast<int>(g[i]);
                    ++cnt;
                }
            }
            return cnt > 0 ? sum / cnt : 0.0;
        };
        if (warm_delta(true) <= warm_delta(false)) {
            return "light_leak did not warm the frame edges";
        }
    }

    // film_flicker: mean luma must wander over time, deterministically.
    {
        double lo = 1e9, hi = -1e9;
        for (double t = 0.0; t < 2.0; t += 0.05) {
            std::vector<std::uint8_t> f = flat(120);
            film("xer.fx.film_flicker", &f, {0.5f, 6.f, 3.f}, t, 11);
            const double m = mean_luma(f);
            lo = std::min(lo, m);
            hi = std::max(hi, m);
        }
        if (hi - lo < 5.0) {
            return "film_flicker did not modulate luma";
        }
        std::vector<std::uint8_t> a = flat(120), b = flat(120);
        film("xer.fx.film_flicker", &a, {0.5f, 6.f, 3.f}, 0.31, 11);
        film("xer.fx.film_flicker", &b, {0.5f, 6.f, 3.f}, 0.31, 11);
        if (a != b) {
            return "film_flicker is not deterministic at fixed time";
        }
    }

    // dust_scratch: sparse deterministic specks on a flat field.
    {
        std::vector<std::uint8_t> g = flat(80), g2 = flat(80);
        film("xer.fx.dust_scratch", &g, {0.9f, 11.f}, 0.5, 3);
        film("xer.fx.dust_scratch", &g2, {0.9f, 11.f}, 0.5, 3);
        if (g != g2) {
            return "dust_scratch is not deterministic at fixed time";
        }
        int changed = 0;
        for (std::size_t i = 0; i < g.size(); i += 4) {
            if (std::abs(static_cast<int>(g[i]) - 80) > 15) ++changed;
        }
        const int npx = kW * kH;
        if (changed < 10) {
            return "dust_scratch produced no specks";
        }
        if (changed > static_cast<int>(npx * 0.9)) {
            return "dust_scratch is not sparse";
        }
    }

    // vignette2 (pure ABI): corners darken, center is preserved.
    {
        std::vector<std::uint8_t> f = flat(200);
        const PluginDesc* d =
            PluginHost::ForEffectType("builtin:xer.fx.vignette2");
        if (!PluginHost::ApplyVideo(*d, f.data(), kW, kH, kStride,
                                    {1.0f, 0.5f, 0.4f, 1.0f})) {
            return "vignette2 apply failed";
        }
        const std::uint8_t center =
            f[(static_cast<size_t>(18) * kW + 32) * 4];
        const std::uint8_t corner = f[0];
        if (center < 190 || corner > 60 || corner >= center) {
            return "vignette2 falloff is wrong";
        }
    }

    // End-to-end through the real compositor path: a generated clip
    // carrying film_grain renders bit-identically across sessions at
    // the same t (deterministic dispatch) and animates across t.
    {
        const auto render = [](double t,
                               std::vector<std::uint8_t>* out) {
            Project project = MakeRenderProject(kW, kH, 15);
            project.clips[0].effects.push_back(
                {"builtin:xer.fx.film_grain", {30.f, 1.f, 2.f, 5.f}});
            Compositor compositor;
            std::string err;
            if (!compositor.Init(project, {}, &err)) return false;
            const bool ok = compositor.RenderFrameAt(t, out, &err);
            compositor.Shutdown();
            return ok;
        };
        std::vector<std::uint8_t> a, b, c;
        if (!render(0.5, &a) || !render(0.5, &b) || !render(0.6, &c)) {
            return "film_fx compositor render failed";
        }
        if (a != b) {
            return "film_grain render is not deterministic";
        }
        if (a == c) {
            return "film_grain did not animate through the compositor";
        }
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

// Records the path its Open() is called with — proves WHICH media
// path a compositor session resolves to (proxy vs. original), the
// observable half of CompositorOptions::media_resolver. Frames are a
// trivial solid 16x16 — the check only cares about the open path.
class RecordingDecoder final : public IMediaDecoder {
public:
    explicit RecordingDecoder(std::string* opened) : opened_(opened) {}
    bool Open(const std::string& utf8_path,
              std::string* /*error_out*/) override {
        if (opened_ != nullptr) *opened_ = utf8_path;
        return true;
    }
    void Close() override {}
    bool IsOpen() const override { return true; }
    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        if (frames_ >= 8) return false;
        ++frames_;
        pos_ += 1.0 / 15.0;
        bgra->assign(16u * 16u * 4u, 200);
        *width = 16;
        *height = 16;
        return true;
    }
    bool Seek(double seconds) override {
        pos_ = std::max(0.0, seconds);
        return true;
    }
    double GetPositionSeconds() const override { return pos_; }
    double GetFps() const override { return 15.0; }
    double GetDurationSeconds() const override { return 0.6; }
    int GetWidth() const override { return 16; }
    int GetHeight() const override { return 16; }
    void EnableAudioTap(bool) override {}
    bool HasAudio() const override { return false; }
    double GetAudioStartSeconds() const override { return 0.0; }
    double GetAudioTimeBase() const override { return 0.0; }
    bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* /*out*/,
        int /*max_frames*/) override {
        return false;
    }
    bool AudioDrained() const override { return true; }
    std::uint64_t GetAudioEpoch() const override { return 0; }

private:
    std::string* opened_;
    int frames_ = 0;
    double pos_ = 0.0;
};

// ProxyManager end-to-end (proxy_manager.cc) over real libx264
// fixtures: import -> Request -> pending -> ready|failed catalog
// transitions, the managed proxy file materializing SMALLER than the
// source, ResolveDecodePath honoring the size+mtime fingerprint, the
// compositor media_resolver seam routing preview decode through the
// proxy, and — the integrity contract — a resolver-less (export-
// shaped) compositor decoding the original path verbatim.
std::string CheckProxyWorkflow() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const auto src = SmokeTempDir() / "proxy_source.mp4";
    const auto src2 = SmokeTempDir() / "proxy_source_fail.mp4";
    const std::string db_path =
        Utf8Path(SmokeTempDir() / "proxy_catalog.db");
    {
        std::error_code ec;
        std::filesystem::remove(src, ec);
        std::filesystem::remove(src2, ec);
        std::filesystem::remove(Path::FromUtf8(db_path), ec);
    }
    // 256x144, 8 frames @15fps — larger than the 64px proxy cap so the
    // build actually transcodes (the under-cap skip would leave no row).
    const auto encode_fixture = [](const std::filesystem::path& out,
                                   std::string* err) -> bool {
        FFmpeg::EncodeVideoParams params;
        params.output_path = Utf8Path(out);
        params.codec_name = "libx264";
        params.width = 256;
        params.height = 144;
        params.pixel_format = "bgra";
        params.fps_num = 15;
        const auto provider = [](std::size_t index,
                                 FFmpeg::VideoFrameInfo* f,
                                 std::string*) -> bool {
            f->width = 256;
            f->height = 144;
            f->pixel_format = "bgra";
            f->pts = f->best_effort_timestamp =
                static_cast<std::int64_t>(index);
            f->line_sizes = {256 * 4, 0, 0, 0};
            f->data.assign(256u * 144u * 4u,
                           static_cast<std::uint8_t>(index));
            return true;
        };
        return FFmpeg::EncodeVideoFrameStream(params, 8, provider, err);
    };
    std::string err;
    if (!encode_fixture(src, &err)) {
        return "fixture encode failed: " + err;
    }
    // A second VALID fixture — corrupted after its catalog row exists
    // so Transcode's decoder open fails and drives pending -> failed.
    if (!encode_fixture(src2, &err)) {
        return "second fixture encode failed: " + err;
    }

    MediaCatalog catalog(db_path);
    if (!catalog.Open(&err) || !catalog.IsOpen()) {
        return "catalog open failed: " + err;
    }
    const std::string src_utf8 = Utf8Path(src);
    const std::string src2_utf8 = Utf8Path(src2);
    std::int64_t id = -1;
    if (!catalog.Import(src_utf8, &id, false, &err) ||
        !catalog.Import(src2_utf8, &id, false, &err)) {
        catalog.Close();
        return "fixture import failed: " + err;
    }
    {
        CatalogEntry entry;
        if (!catalog.FindByPath(src_utf8, &entry, &err) ||
            !entry.HasVideo()) {
            catalog.Close();
            return "imported fixture has no video row";
        }
    }
    // Corrupt src2 AFTER its row exists — the row's probe data carries
    // Request past its front gates; Transcode's decoder open then
    // fails, driving the pending -> failed transition.
    {
        std::ofstream junk(Path::FromUtf8(src2_utf8),
                           std::ios::binary | std::ios::trunc);
        junk << "not a media file at all";
    }

    ProxyManager proxies(&catalog);
    ProxySettings settings;
    settings.max_dim = 64;
    settings.crf = 30;
    if (!proxies.Request(src_utf8, settings)) {
        catalog.Close();
        return "Request rejected a valid video fixture";
    }
    if (!proxies.Request(src2_utf8, settings)) {
        catalog.Close();
        return "Request rejected the doomed fixture";
    }
    // The pending row lands synchronously inside Request; the worker
    // may already have flipped it by the time we read, so accept
    // either post-pending state — but a row must exist.
    {
        ProxyRecord early;
        if (!catalog.GetProxy(src_utf8, &early) ||
            (early.status != ProxyStatus::kPending &&
             early.status != ProxyStatus::kReady)) {
            catalog.Close();
            return "pending row never landed for the fixture";
        }
    }
    if (!proxies.WaitIdle(60000)) {
        catalog.Close();
        return "proxy worker did not go idle in 60s";
    }

    // ---- ready: row + managed file + downscale -----------------------
    ProxyRecord rec;
    if (!catalog.GetProxy(src_utf8, &rec, &err) ||
        rec.status != ProxyStatus::kReady || rec.proxy_path.empty()) {
        catalog.Close();
        return "proxy row is not ready: " + err;
    }
    {
        std::error_code ec;
        if (!std::filesystem::exists(Path::FromUtf8(rec.proxy_path),
                                     ec)) {
            catalog.Close();
            return "ready row points at a missing proxy file";
        }
    }
    {
        FFmpeg::MediaInfo info;
        if (!FFmpeg::ProbeMedia(rec.proxy_path, &info, &err)) {
            catalog.Close();
            return "proxy probe failed: " + err;
        }
        const FFmpeg::StreamInfo* video = nullptr;
        for (const auto& stream : info.streams) {
            if (stream.media_type == "video") video = &stream;
        }
        if (video == nullptr) {
            catalog.Close();
            return "proxy has no video stream";
        }
        if (video->width >= 256 || video->height >= 144) {
            catalog.Close();
            return "proxy is not smaller than the source";
        }
        if (video->width != 64 || video->height != 36) {
            catalog.Close();
            return "proxy dims " + std::to_string(video->width) + "x" +
                   std::to_string(video->height) + " != expected 64x36";
        }
    }

    // ---- failed: the corrupted fixture -------------------------------
    {
        ProxyRecord failed;
        if (!catalog.GetProxy(src2_utf8, &failed, &err) ||
            failed.status != ProxyStatus::kFailed ||
            failed.error.empty()) {
            catalog.Close();
            return "corrupt fixture did not land in failed state";
        }
    }

    // ---- decode-path resolution ---------------------------------------
    if (proxies.ResolveDecodePath(src_utf8) != rec.proxy_path) {
        catalog.Close();
        return "ResolveDecodePath did not return the ready proxy";
    }

    // Compositor open-path assertions through the RecordingDecoder:
    // the resolver-installed session must open the proxy; the
    // resolver-less session — ExportEngine's exact option shape —
    // must open the original path VERBATIM (a proxy reaching export
    // is a data-integrity bug).
    Project project;
    project.width = 32;
    project.height = 32;
    project.fps_num = 15;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip clip;
    clip.id = "c1";
    clip.track = 0;
    clip.timeline_start = 0.0;
    clip.duration = 0.5;
    clip.source.kind = SourceKind::Media;
    clip.source_path = src_utf8;
    project.clips.push_back(clip);

    std::string opened;
    const auto recording_factory = [&opened] {
        return MediaDecoderPtr(new RecordingDecoder(&opened));
    };
    {
        CompositorOptions options;
        options.decoder_factory = recording_factory;
        options.media_resolver = [&proxies](const std::string& p) {
            return proxies.ResolveDecodePath(p);
        };
        Compositor compositor;
        if (!compositor.Init(project, options, &err)) {
            catalog.Close();
            return "compositor init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        std::string render_err;
        compositor.RenderFrameAt(0.1, &bgra, &render_err);
        compositor.Shutdown();
        if (opened != rec.proxy_path) {
            catalog.Close();
            return "resolver compositor opened '" + opened +
                   "' — expected the proxy '" + rec.proxy_path + "'";
        }
    }
    {
        opened.clear();
        CompositorOptions options;   // export's shape: no resolver
        options.decoder_factory = recording_factory;
        Compositor compositor;
        if (!compositor.Init(project, options, &err)) {
            catalog.Close();
            return "export-shaped compositor init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        std::string render_err;
        compositor.RenderFrameAt(0.1, &bgra, &render_err);
        compositor.Shutdown();
        if (opened != src_utf8) {
            catalog.Close();
            return "resolver-less compositor opened '" + opened +
                   "' — export must only ever see the original";
        }
    }

    // ---- fingerprint: a rewritten source falls back to original -------
    {
        std::ofstream grow(Path::FromUtf8(src_utf8),
                           std::ios::binary | std::ios::app);
        grow << "tail-bytes-change-the-fingerprint";
    }
    if (proxies.ResolveDecodePath(src_utf8) != src_utf8) {
        catalog.Close();
        return "stale-fingerprint proxy still resolved after rewrite";
    }

    // ---- storage cap: protected sources + LRU eviction ---------------
    // Protected sources are never evicted (the open project's clips);
    // unprotected ready proxies go oldest-mtime-first — the file mtime
    // IS the last-used stamp (ResolveDecodePath rewrites it).
    proxies.SetProtectedSources({src_utf8});
    proxies.SetStorageCapBytes(1);   // 1 byte — everything overflows
    if (proxies.EnforceStorageCap() != 0) {
        catalog.Close();
        return "protected proxy was evicted";
    }
    {
        ProxyRecord still;
        if (!catalog.GetProxy(src_utf8, &still) ||
            still.status != ProxyStatus::kReady) {
            catalog.Close();
            return "protected proxy row vanished";
        }
    }
    // LRU order: two fake ready rows + files under the managed dir;
    // a cap that fits only one must drop the older file, keep the
    // newer, and remove the evicted catalog row.
    const std::string proxy_dir = catalog.ProxyDir();
    const std::string canon_a = "z:/xer_smoke_evict_a.mp4";
    const std::string canon_b = "z:/xer_smoke_evict_b.mp4";
    const auto plant_proxy = [&](const std::string& canon,
                                 int age_seconds) {
        const auto file = Path::FromUtf8(
            proxy_dir + "/" + ProxyManager::ProxyFileName(canon));
        {
            std::ofstream o(file, std::ios::binary | std::ios::trunc);
            o << std::string(4096, 'x');
        }
        std::error_code ec;
        std::filesystem::last_write_time(
            file,
            std::filesystem::file_time_type::clock::now() -
                std::chrono::seconds(age_seconds),
            ec);
        ProxyRecord planted;
        planted.path = canon;
        planted.proxy_path = Utf8Path(file);
        planted.status = ProxyStatus::kReady;
        if (!catalog.SetProxyRecord(planted, &err)) {
            return std::filesystem::path{};
        }
        return file;
    };
    const std::filesystem::path file_a = plant_proxy(canon_a, 7200);
    const std::filesystem::path file_b = plant_proxy(canon_b, 0);
    if (file_a.empty() || file_b.empty()) {
        catalog.Close();
        return "failed to plant LRU fixture rows";
    }
    proxies.SetStorageCapBytes(6000);   // fits one of the two 4 KiB files
    const int evicted = proxies.EnforceStorageCap();
    if (evicted != 1) {
        std::error_code ec;
        const bool a_exists = std::filesystem::exists(file_a, ec);
        const bool b_exists = std::filesystem::exists(file_b, ec);
        catalog.Close();
        return "LRU sweep did not evict exactly one proxy"
               " (evicted=" + std::to_string(evicted) +
               " a_exists=" + std::to_string(a_exists) +
               " b_exists=" + std::to_string(b_exists) + ")";
    }
    {
        std::error_code ec;
        if (std::filesystem::exists(file_a, ec)) {
            catalog.Close();
            return "LRU evicted the freshest proxy";
        }
        if (!std::filesystem::exists(file_b, ec)) {
            catalog.Close();
            return "LRU dropped the newest proxy file";
        }
        ProxyRecord gone;
        if (catalog.GetProxy(canon_a, &gone)) {
            catalog.Close();
            return "evicted proxy row still present";
        }
    }
    // Drop protection — the real proxy (oldest usage stamp) goes next.
    proxies.SetProtectedSources({});
    proxies.SetStorageCapBytes(1);
    if (proxies.EnforceStorageCap() < 1) {
        catalog.Close();
        return "unprotected ready proxies were not evicted";
    }
    {
        std::error_code ec;
        if (std::filesystem::exists(Path::FromUtf8(rec.proxy_path),
                                    ec)) {
            catalog.Close();
            return "evicted proxy file still on disk";
        }
        ProxyRecord gone;
        if (catalog.GetProxy(src_utf8, &gone)) {
            catalog.Close();
            return "evicted source row still in catalog";
        }
    }
    catalog.Close();
    return {};
}

// Records::RecentProjectsStore (records_store.h) — the SQLite recent-
// projects module behind the start screen. Exercises the full file
// contract: schema create, insert/touch recency (a re-touch must jump
// the rowid tiebreak even within one wall-clock second), sentinel
// metadata merge, Remove, the recency-preserving UpdateMeta, and — the
// actual point of a persistent store — data surviving Close/Open on
// the same file.
std::string CheckRecordsStore() {
    const std::string db_path =
        Utf8Path(SmokeTempDir() / "records_store.db");
    std::error_code ec;
    std::filesystem::remove(Path::FromUtf8(db_path), ec);  // fresh run
    const std::string dir = Utf8Path(SmokeTempDir());
    {
        Records::RecentProjectsStore store(db_path);
        std::string err;
        if (!store.Open(&err) || !store.IsOpen()) {
            return "Open failed: " + err;
        }
        if (!store.AddOrTouch(dir + "/alpha.xerproj", "Alpha", 12.5,
                              "", &err) ||
            !store.AddOrTouch(dir + "/beta.xerproj", {}, 0.0, "",
                              &err) ||
            !store.AddOrTouch(dir + "/gamma.xerproj", "Gamma", -1.0,
                              "posters/g.png", &err)) {
            return "AddOrTouch failed: " + err;
        }
        // Re-touch alpha — it must move to the front (fresh rowid
        // tiebreak inside the same timestamp second).
        if (!store.AddOrTouch(dir + "/alpha.xerproj", "Alpha", 12.5,
                              "", &err)) {
            return "re-touch failed: " + err;
        }
        std::vector<Records::RecentProject> rows = store.List(0, &err);
        if (!err.empty()) {
            return "List failed: " + err;
        }
        if (rows.size() != 3) {
            return "expected 3 rows, got " + std::to_string(rows.size());
        }
        if (!rows[0].path.ends_with("alpha.xerproj") ||
            !rows[1].path.ends_with("gamma.xerproj") ||
            !rows[2].path.ends_with("beta.xerproj")) {
            return "recency order broken after re-touch";
        }
        if (rows[0].display_name != "Alpha" ||
            rows[0].duration_seconds < 12.0) {
            return "metadata not stored on the touched row";
        }
        // beta had no display_name — the file-stem fallback applies.
        if (rows[2].display_name != "beta") {
            return "display_name stem fallback broken: '" +
                   rows[2].display_name + "'";
        }
        // gamma's -1 duration sentinel normalizes to 0; its poster
        // round-trips.
        if (rows[1].duration_seconds != 0.0 ||
            rows[1].poster_path != "posters/g.png") {
            return "sentinel/poster round-trip broken";
        }
        // UpdateMeta: fields merge, ordering is NOT perturbed.
        Records::RecentProject patch;
        patch.path = dir + "/beta.xerproj";
        patch.display_name = "Beta cut";
        patch.poster_path = "posters/b.png";
        if (!store.UpdateMeta(patch, &err)) {
            return "UpdateMeta failed: " + err;
        }
        rows = store.List(0, &err);
        if (rows.size() != 3 ||
            !rows[0].path.ends_with("alpha.xerproj") ||
            !rows[2].path.ends_with("beta.xerproj")) {
            return "UpdateMeta perturbed recency order";
        }
        if (rows[2].display_name != "Beta cut" ||
            rows[2].poster_path != "posters/b.png") {
            return "UpdateMeta fields did not merge";
        }
        if (!store.Remove(dir + "/gamma.xerproj", &err)) {
            return "Remove failed: " + err;
        }
        rows = store.List(0, &err);
        if (rows.size() != 2) {
            return "Remove left " + std::to_string(rows.size()) +
                   " rows";
        }
        store.Close();
    }
    // Second open on the same file — the persistence contract.
    Records::RecentProjectsStore store(db_path);
    std::string err;
    if (!store.Open(&err)) {
        return "reopen failed: " + err;
    }
    const std::vector<Records::RecentProject> rows = store.List(0, &err);
    if (rows.size() != 2 ||
        !rows[0].path.ends_with("alpha.xerproj") ||
        !rows[1].path.ends_with("beta.xerproj")) {
        store.Close();
        return "persistence across reopen broken";
    }
    if (rows[0].last_opened_unix <= 0) {
        store.Close();
        return "last_opened timestamp not persisted";
    }
    store.Close();
    std::filesystem::remove(Path::FromUtf8(db_path), ec);
    return {};
}

// Headless start-screen model (records_store.h display helpers — the
// part of the ProjectManager that can run without a window):
// FormatTimeAgo's band edges and MakeRecentTile's meta composition.
std::string CheckProjectManager() {
    constexpr std::int64_t kNow = 1'700'000'000;  // 2023-11-14
    const auto ago = [kNow](std::int64_t t) {
        return Records::FormatTimeAgo(t, kNow);
    };
    if (ago(kNow - 5) != "just now" ||
        ago(kNow + 60) != "just now") {      // clock skew clamps
        return "sub-minute band broken";
    }
    if (ago(kNow - 60) != "1 minute ago" ||
        ago(kNow - 120) != "2 minutes ago") {
        return "minutes band broken";
    }
    if (ago(kNow - 3600) != "1 hour ago" ||
        ago(kNow - 7200) != "2 hours ago") {
        return "hours band broken";
    }
    if (ago(kNow - 86400) != "yesterday" ||
        ago(kNow - 3 * 86400) != "3 days ago") {
        return "days band broken";
    }
    // Older than a week -> absolute "YYYY-MM-DD".
    const std::string old = ago(kNow - 30 * 86400);
    if (old.size() != 10 || old[4] != '-' || old[7] != '-') {
        return "absolute-date band broken: '" + old + "'";
    }
    if (!ago(0).empty()) {
        return "zero timestamp must yield an empty label";
    }

    Records::RecentProject rec;
    rec.path = "C:/work/promo_v2.xerproj";
    rec.last_opened_unix = kNow - 300;
    rec.duration_seconds = 65.0;   // -> "1:05"
    rec.poster_path = "posters/p.png";
    Records::RecentTile tile = Records::MakeRecentTile(rec, kNow);
    if (tile.title != "promo_v2") {
        return "tile stem fallback broken: '" + tile.title + "'";
    }
    if (tile.meta_line != "5 minutes ago \xc2\xb7 1:05") {
        return "tile meta composition broken: '" + tile.meta_line +
               "'";
    }
    if (tile.poster_path != "posters/p.png") {
        return "tile poster not carried";
    }
    rec.display_name = "Promo v2";
    rec.duration_seconds = 0.0;
    tile = Records::MakeRecentTile(rec, kNow);
    if (tile.title != "Promo v2" ||
        tile.meta_line != "5 minutes ago") {
        return "tile name/no-duration composition broken";
    }
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

// `generated_layers` — the "Add Color Matte / Color Bars / Text"
// flow headless end to end: clips are created through the MODEL ops
// (AddTrack + AddClip with unbounded video bounds — the same calls
// the app's AddGeneratedClipAt issues through TimelineHistory),
// composited through the real Compositor (each kind must produce its
// signature pixels — proving no clip took a decoder path), then the
// project round-trips through ProjectStore so source.kind +
// source.param + the Clip::name layer label all survive save/load.
std::string CheckGeneratedLayers() {
    Project project;
    project.width = 160;
    project.height = 90;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;
    if (AddTrack(project, TrackKind::Video, "V1") != 0 ||
        AddTrack(project, TrackKind::Video, "V2") != 1) {
        return "AddTrack failed";
    }

    const MediaBounds gen_bounds{0.0, true};   // unbounded, has video
    Clip matte = MakeGeneratedClip("matte", 0, 0.0, 6.0,
                                   SourceKind::ColorMatte, "#3366CC");
    matte.name = "Backdrop";
    Clip bars = MakeGeneratedClip("bars", 1, 0.0, 3.0,
                                  SourceKind::ColorBars, "smpte75");
    Clip text = MakeGeneratedClip("title", 1, 3.0, 3.0,
                                  SourceKind::Text, "Smoke | size=24");
    text.name = "Smoke Title";
    for (const Clip* clip : {&matte, &bars, &text}) {
        if (!AddClip(project, *clip, gen_bounds, &err)) {
            return "AddClip rejected a generated clip: " + err;
        }
    }

    Compositor compositor;
    if (!compositor.Init(project, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const auto pixel = [](const std::vector<std::uint8_t>& buf,
                          int x, int y) {
        const auto* p =
            buf.data() + (static_cast<std::size_t>(y) * 160 + x) * 4u;
        return std::array<std::uint8_t, 4>{p[0], p[1], p[2], p[3]};
    };

    // t=1.5: V2 SMPTE-75 bars composite over the V1 matte — the first
    // column is 191 gray, the last is blue (BGRA byte order).
    if (!compositor.RenderFrameAt(1.5, &bgra, &err)) {
        compositor.Shutdown();
        return "RenderFrameAt(bars) failed: " + err;
    }
    if (bgra.size() != static_cast<std::size_t>(160) * 90 * 4) {
        compositor.Shutdown();
        return "rendered buffer has the wrong size";
    }
    if (pixel(bgra, 10, 45) !=
            std::array<std::uint8_t, 4>{191, 191, 191, 255} ||
        pixel(bgra, 159, 45) !=
            std::array<std::uint8_t, 4>{191, 0, 0, 255}) {
        compositor.Shutdown();
        return "ColorBars pixels wrong at t=1.5";
    }

    // t=4.5: V2 text composites over the matte — a bottom-right pixel
    // is the matte color (#3366CC -> BGRA CC,66,33 — hex parse proven
    // end to end) while a bright glyph pixel must exist somewhere
    // (the text actually drew).
    if (!compositor.RenderFrameAt(4.5, &bgra, &err)) {
        compositor.Shutdown();
        return "RenderFrameAt(text) failed: " + err;
    }
    compositor.Shutdown();
    if (pixel(bgra, 159, 89) !=
        std::array<std::uint8_t, 4>{0xCC, 0x66, 0x33, 0xFF}) {
        return "matte underlay pixel wrong at t=4.5";
    }
    const bool any_glyph = std::any_of(
        bgra.begin(), bgra.end(),
        [](std::uint8_t byte) { return byte >= 200; });
    if (!any_glyph) {
        return "text layer produced no glyph pixels";
    }

    // Persistence: kind + param + name + placement all survive a
    // .xerproj round trip — the LAYERS panel reads Clip::name, so a
    // silent drop would strip every user-facing label on reload.
    const std::string path =
        Utf8Path(SmokeTempDir() / "generated_layers.xerproj");
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    const struct {
        const char* id;
        SourceKind kind;
        const char* param;
        const char* name;
        int track;
        double start;
        double duration;
    } kExpect[] = {
        {"matte", SourceKind::ColorMatte, "#3366CC", "Backdrop",
         0, 0.0, 6.0},
        {"bars", SourceKind::ColorBars, "smpte75", "", 1, 0.0, 3.0},
        {"title", SourceKind::Text, "Smoke | size=24", "Smoke Title",
         1, 3.0, 3.0},
    };
    for (const auto& expect : kExpect) {
        const Clip* clip = FindClip(loaded, expect.id);
        if (clip == nullptr || clip->source.kind != expect.kind ||
            clip->source.param != expect.param ||
            clip->name != expect.name || clip->track != expect.track ||
            clip->timeline_start != expect.start ||
            clip->duration != expect.duration) {
            return "generated clip did not survive the round trip: " +
                   std::string(expect.id);
        }
    }
    return {};
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

// LUT .cube parser (lut_manager.h) — synthetic strings only; no file
// system or OCIO needed. TITLE/size/data extraction on a well-formed
// 3D cube, tolerant-but-warned short bodies, hard reject on cubes with
// no declared size or no data rows, and the 1D variant.
std::string CheckLutManager() {
    const std::string cube =
        "# leading comment\n"
        "TITLE \"Smoky Two\"\n"
        "LUT_3D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 1 1 1\n"
        "0 0 0\n1 0 0\n0 1 0\n0 0 1\n"
        "1 1 0\n1 0 1\n0 1 1\n1 1 1 # trailing comment\n";
    Lut::CubeData data;
    std::string error;
    if (!Lut::ParseCubeText(cube, &data, &error)) {
        return "ParseCubeText rejected a valid cube: " + error;
    }
    if (data.title != "Smoky Two" || !data.is_3d || data.size != 2 ||
        data.data.size() != 24) {
        return "ParseCubeText produced wrong title/size/data";
    }
    // Short body: parse succeeds but surfaces the row-count warning.
    error.clear();
    if (!Lut::ParseCubeText("LUT_3D_SIZE 2\n0 0 0\n", &data, &error) ||
        error.empty()) {
        return "short cube body missed its warning";
    }
    // No declared size / no data rows are hard failures.
    if (Lut::ParseCubeText("TITLE \"x\"\n0 0 0\n", &data, nullptr)) {
        return "size-less cube accepted";
    }
    if (Lut::ParseCubeText("LUT_3D_SIZE 2\n", &data, nullptr)) {
        return "empty-body cube accepted";
    }
    // 1D variant: size rows, not size^3.
    error.clear();
    if (!Lut::ParseCubeText("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n", &data,
                            &error) ||
        data.is_3d || data.size != 2 || data.data.size() != 6) {
        return "1D cube parse failed";
    }
    return {};
}

// URL import helpers (url_import.h) — table-driven classification
// (platform host / direct / invalid) plus the filename machinery:
// Content-Disposition extraction, URL basename, percent-decoding and
// Windows filename sanitizing. No network — Download() itself is
// exercised live, never in smoke.
std::string CheckUrlImport() {
    using UrlKind = UrlImport::UrlKind;
    const struct {
        const char* url;
        UrlKind kind;
    } cases[] = {
        {"https://www.youtube.com/watch?v=abc", UrlKind::kPlatform},
        {"https://youtu.be/abc", UrlKind::kPlatform},
        {"https://m.youtube.com/watch?v=x", UrlKind::kPlatform},
        {"https://vimeo.com/12345", UrlKind::kPlatform},
        {"https://www.tiktok.com/@u/video/1", UrlKind::kPlatform},
        {"https://www.instagram.com/reel/x/", UrlKind::kPlatform},
        {"https://clips.twitch.tv/x", UrlKind::kPlatform},
        {"HTTPS://YOUTUBE.COM/watch?v=x", UrlKind::kPlatform},
        // Suffix lookalikes must NOT classify as the platform.
        {"https://notyoutube.com/x", UrlKind::kDirect},
        {"https://youtube.com.evil.com/x", UrlKind::kDirect},
        {"https://cdn.example.com/clip.mp4", UrlKind::kDirect},
        {"http://files.host/a.wav?dl=1", UrlKind::kDirect},
        {"https://user@h.example.com/m.mkv", UrlKind::kDirect},
        {"ftp://h/f.mp4", UrlKind::kInvalid},
        {"file:///c:/a.mp4", UrlKind::kInvalid},
        {"C:/media/a.mp4", UrlKind::kInvalid},
        {"youtube.com/watch?v=x", UrlKind::kInvalid},
        {"https:///x", UrlKind::kInvalid},
        {"", UrlKind::kInvalid},
    };
    for (const auto& c : cases) {
        if (UrlImport::ClassifyUrl(c.url) != c.kind) {
            return std::string("ClassifyUrl(\"") + c.url +
                   "\") wrong kind";
        }
    }
    // Host extraction (used for status text + platform match).
    if (UrlImport::HostOf("https://YouTube.COM:443/watch") !=
        "youtube.com") {
        return "HostOf failed port/case handling";
    }
    // Filename machinery.
    if (UrlImport::FilenameFromContentDisposition(
            "attachment; filename=\"clip final.mp4\"") !=
        "clip final.mp4") {
        return "Content-Disposition quoted filename failed";
    }
    if (UrlImport::FilenameFromContentDisposition(
            "attachment; filename*=UTF-8''%E2%82%AC%20clip.mp4") !=
        "\xE2\x82\xAC clip.mp4") {
        return "Content-Disposition RFC5987 filename failed";
    }
    if (!UrlImport::FilenameFromContentDisposition("inline").empty()) {
        return "Content-Disposition phantom filename";
    }
    if (UrlImport::UrlBasename(
            "https://h/a/b/my%20clip.mp4?dl=1#f") != "my clip.mp4" ||
        !UrlImport::UrlBasename("https://h/a/").empty() ||
        !UrlImport::UrlBasename("https://h").empty()) {
        return "UrlBasename failed";
    }
    if (UrlImport::PercentDecode("%41%42") != "AB") {
        return "PercentDecode failed";
    }
    struct {
        const char* in;
        const char* want;
    } sanitize[] = {
        {"a<b>c:d\"e/f\\g|h?i*j", "a_b_c_d_e_f_g_h_i_j"},
        {"clip.mp4", "clip.mp4"},
        {"name.", "name"},
        {"...", "download"},
        {"", "download"},
    };
    for (const auto& c : sanitize) {
        if (UrlImport::SanitizeFilename(c.in) != c.want) {
            return std::string("SanitizeFilename(\"") + c.in +
                   "\") wrong";
        }
    }
    return {};
}

// Extensions panel data path: when assets/extensions/ holds .xexrj
// packages, ExtHost::Shared().Scan() must surface them to the Ext
// dock tab's list (id/name/version present) and a UI-declaring
// extension must resolve a usable xer-ext:// URL + member bytes —
// exactly what the panel's "Open" action consumes. Missing assets
// are tolerated (fresh builds may deploy none).
std::string CheckExtensionsPanelData() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);  // resolves assets_dir
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return {};   // no assets deployed — nothing to list
    }
    const std::filesystem::path ext_dir =
        cfg.assets_dir / "extensions";
    std::error_code ec;
    bool any_package = false;
    if (std::filesystem::is_directory(ext_dir, ec)) {
        for (const auto& de :
             std::filesystem::directory_iterator(ext_dir, ec)) {
            if (de.path().extension() == ".xexrj") {
                any_package = true;
                break;
            }
        }
    }
    if (!any_package) {
        return {};   // tolerant: extensions are optional content
    }
    ExtHost::Shared().Scan(ext_dir.string());
    const auto& entries = ExtHost::Shared().Entries();
    if (entries.empty()) {
        return "extensions present but scan listed none";
    }
    int verified = 0;
    const Extension* with_ui = nullptr;
    for (const Extension& e : entries) {
        if (!e.verified) continue;
        ++verified;
        if (e.manifest.id.empty() || e.manifest.name.empty()) {
            return "verified extension missing id/name in manifest";
        }
        if (!e.manifest.ui.empty() && with_ui == nullptr) {
            with_ui = &e;
        }
    }
    if (verified == 0) {
        return "extensions present but none verified";
    }
    // The six advanced extensions are build-time packed from
    // assets/extensions_src/<id>/ — when the deployment carries
    // extension packages at all, each of them must be present,
    // verified, and expose its declared UI member.
    const std::vector<std::string> required_ids = {
        "beatcutter", "captionist", "silencecutter",
        "autoduck", "scenecutter", "audiomix",
    };
    for (const std::string& id : required_ids) {
        const Extension* hit = nullptr;
        for (const Extension& e : entries) {
            if (e.manifest.id == id) { hit = &e; break; }
        }
        if (hit == nullptr) {
            return "required extension not scanned: " + id;
        }
        if (!hit->verified) {
            return "required extension not verified: " + id;
        }
        if (hit->manifest.ui.empty()) {
            return "required extension has no UI member: " + id;
        }
        if (ExtHost::Shared()
                .ReadMember(id, hit->manifest.ui)
                .empty()) {
            return "UI member unreadable for " + id + ": " +
                   hit->manifest.ui;
        }
    }
    if (with_ui != nullptr) {
        // The panel builds this URL for its Open action.
        const std::string url =
            XER::Cef::ExtSchemeUrl(with_ui->manifest.id,
                                   with_ui->manifest.ui);
        const std::string want_prefix =
            "xer-ext://" + with_ui->manifest.id + "/";
        if (url.rfind(want_prefix, 0) != 0) {
            return "ExtSchemeUrl produced '" + url + "'";
        }
        if (ExtHost::Shared()
                .ReadMember(with_ui->manifest.id, with_ui->manifest.ui)
                .empty()) {
            return "declared UI member unreadable: " +
                   with_ui->manifest.ui;
        }
    }
    return {};
}

// .xexrj native JIT payloads (tools/tz/xsjo_compile.*): ztzmake packs
// extension C++ sources as COFF objects under mobjective/*.xsjo, each
// wrapped in an XSJ1 header (magic, version, triple, source name).
// The JIT loader trusts that header before executing the object, so
// this check re-validates it on every deployed extension: well-formed
// header + a COFF machine word matching the triple. No .xexrj or no
// .xsjo member degrades gracefully (assets_icons owns missing-package
// complaints, and non-Clang toolchains legitimately ship none); a
// malformed .xsjo is a hard failure — the pipeline produced a broken
// object.
std::string CheckExtXsjo() {
    std::string status;
    AppRuntime::Boot(&status);   // resolves assets_dir
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return {};
    }
    const std::filesystem::path ext_dir = cfg.assets_dir / "extensions";
    std::error_code ec;
    if (!std::filesystem::is_directory(ext_dir, ec)) {
        return {};
    }
    const auto u16le = [](const std::uint8_t* p) {
        return static_cast<unsigned>(p[0]) |
               (static_cast<unsigned>(p[1]) << 8);
    };
    const auto u64le = [](const std::uint8_t* p) {
        std::uint64_t x = 0;
        for (int s = 0; s < 64; s += 8) {
            x |= static_cast<std::uint64_t>(p[s / 8]) << s;
        }
        return x;
    };
    for (const auto& de :
         std::filesystem::directory_iterator(ext_dir, ec)) {
        if (de.path().extension() != ".xexrj") continue;
        std::string open_err;
        auto arc =
            Tz::TzArchive::Open(Utf8Path(de.path()), &open_err);
        if (!arc) {
            return "cannot open " +
                   Utf8Path(de.path().filename()) + ": " + open_err;
        }
        for (const auto& name : arc->Names()) {
            if (name.rfind("mobjective/", 0) != 0 ||
                name.size() < 6 ||
                name.compare(name.size() - 5, 5, ".xsjo") != 0) {
                continue;
            }
            std::vector<std::uint8_t> bytes;
            if (!arc->Read(name, &bytes)) {
                return "cannot read member " + name;
            }
            const std::uint8_t* p = bytes.data();
            const std::size_t n = bytes.size();
            const auto bad = [&](const char* why) {
                return name + ": " + why;
            };
            if (n < 14 || p[0] != 'X' || p[1] != 'S' || p[2] != 'J' ||
                p[3] != '1') {
                return bad("bad XSJ1 magic");
            }
            if (u16le(p + 4) != 1) {
                return bad("unsupported XSJ1 version");
            }
            std::size_t at = 6;
            std::string triple;
            for (int i = 0; i < 2; ++i) {   // triple, source name
                if (at + 2 > n) return bad("truncated header");
                const unsigned len = u16le(p + at);
                at += 2;
                if (at + len > n) return bad("truncated strings");
                if (i == 0) {
                    triple.assign(
                        reinterpret_cast<const char*>(p + at), len);
                }
                at += len;
            }
            if (triple.empty()) return bad("empty triple");
            if (at + 8 > n) return bad("missing payload size");
            const std::uint64_t payload_size = u64le(p + at);
            at += 8;
            if (payload_size < 20 || payload_size > n - at) {
                return bad("payload size out of range");
            }
            // COFF object file header starts with the machine type:
            // x86_64 triples must carry IMAGE_FILE_MACHINE_AMD64.
            if (triple.rfind("x86_64", 0) == 0 &&
                u16le(p + at) != 0x8664) {
                return bad("payload is not an x86_64 COFF object");
            }
        }
    }
    return {};
}

// VE.ExtensionsRuntime.exe (extensions/ext_runtime_server.cc) — the
// dedicated native-extension JIT host: the exe must sit beside
// video_editor.exe (build/deploy contract), ping must report the
// VE.ExtensionsRuntime role, ext_list must surface the deployed
// demo.xexrj, and ext_load on the JS-only demo must succeed
// gracefully (native:false — no mobjective members → the in-editor
// ExtHost owns it). Any listed extension that DOES carry
// mobjective/*.xsjo is also ext_loaded — tolerated when the JIT is
// unavailable or the reply is a clean ok:false engine error (the
// ztzmake-side native demo may not be packed yet); transport silence
// and malformed replies still count as failures.
std::string CheckExtRuntime() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return "VE.ExtensionsRuntime.exe missing beside "
               "video_editor.exe";
    }
    WorkerPipe pipe(WorkerPipeSpec{
        "VE.ExtensionsRuntime.exe", nullptr, "XER_VE_EXT_WORKER"});
    if (!pipe.Available()) {
        return "ext runtime spawn failed";
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "ext runtime ping failed";
    }
    if (role != "VE.ExtensionsRuntime") {
        return "unexpected ext runtime role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req), 15000,
                                  reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };

    // ext_list — must find the deployed demo.xexrj.
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list failed: " +
               list.get("err", "malformed reply").asString();
    }
    std::string demo_path;
    std::vector<std::string> native_paths;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
        if (e.get("has_native", false).asBool()) {
            native_paths.push_back(e.get("path", "").asString());
        }
    }
    if (demo_path.empty()) {
        return "demo.xexrj not reported by ext_list";
    }

    // ext_load demo — ok:true is required. native:true means the
    // demo's mobjective/*.xsjo went through the real JIT path
    // (XSJ1 parse → COFF → LLJIT link → module loaded); native:false
    // is the graceful JS-only shape.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load)) {
        return "ext_load demo reply is malformed";
    }
    if (!load.get("ok", false).asBool()) {
        return "ext_load demo failed: " +
               load.get("err", "?").asString();
    }
    if (!load.isMember("native")) {
        return "ext_load demo reply lacks the native flag";
    }

    // When the demo packed native objects, drive one exported
    // callable end to end — ext_call "ve_demo_name" exercises the
    // JIT symbol lookup, the JIT'd call itself and the ve_ctx
    // pass-through. ok:true with a non-empty result_json is
    // asserted; ok:false is tolerated (the demo's exports may move).
    if (load.get("native", false).asBool()) {
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_call";
                    r["ext_id"] = "demo";
                    r["fn"] = "ve_demo_name";
                    r["args"] = "{}";
                },
                &reply)) {
            return "ext_call ve_demo_name got no reply";
        }
        Json::Value call;
        if (!parse(reply, &call)) {
            return "ext_call ve_demo_name reply is malformed";
        }
        if (call.get("ok", false).asBool() &&
            call.get("result_json", "").asString().empty()) {
            return "ext_call ve_demo_name returned an empty result";
        }
        // ok:false tolerated — the demo's exported callables may
        // change; the JIT path already ran in ext_load.
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_unload";
                    r["ext_id"] = "demo";
                },
                &reply)) {
            return "ext_unload got no reply";
        }
    }

    // ext_events — drains the queued-event bridge; empty is fine.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) { r["kind"] = "ext_events"; },
            &reply)) {
        return "ext_events got no reply";
    }
    Json::Value ev;
    if (!parse(reply, &ev) || !ev.get("ok", false).asBool() ||
        !ev["events"].isArray()) {
        return "ext_events reply malformed";
    }

    // Tolerated JIT drive — see the comment above the function.
    for (const auto& p : native_paths) {
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_load";
                    r["path"] = p;
                },
                &reply)) {
            return "ext_load of a native extension got no reply";
        }
        if (!parse(reply, &load)) {
            return "ext_load native reply malformed";
        }
        // ok:false tolerated until the native demo lands.
    }
    pipe.KillWorker();
    return {};
#else
    return {};
#endif
}

// ext_bridge — the v2 host channel (sdk/ve.core.h, worker_pipe's
// callback_env reverse channel). Drives the REAL ExtRuntimePipe
// singleton so the spawned VE.ExtensionsRuntime gets the
// XER_VE_EXT_HOST_* handle handoff the editor would give it, then:
//   1. ext_call "ve_demo_add_clip" — the fire-and-forget path; the
//      reply's events[] must carry a well-formed "add_clip" event.
//   2. ext_call "ve_demo_hostcall" on a SECOND thread while this one
//      services the reverse channel — the extension's ve_call parks
//      inside the runtime until our host_call reply unblocks it; the
//      callable echoes the editor's result back through result_json.
// Tolerated: missing exe/JIT, a demo packed before these exports
// existed (ok:false symbol errors), or a worker spawned without the
// channel. Hard failures: transport silence and malformed shapes.
std::string CheckExtBridge() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe& pipe = ExtRuntimePipe();
    if (!pipe.Available()) {
        return {};   // tolerated — spawn may be disabled
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "ext runtime ping failed";
    }
    if (role != "VE.ExtensionsRuntime") {
        return "unexpected ext runtime role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            int timeout_ms, std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  timeout_ms, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };

    // Find + load the demo. native:false/ok:false both skip the rest
    // gracefully (JS-only pack, or no LLVM in this toolchain).
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            15000, &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list reply malformed";
    }
    std::string demo_path;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
    }
    if (demo_path.empty()) {
        return {};   // no demo deployed — nothing to bridge
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            60000, &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load) || !load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable or JS-only pack — tolerated
    }

    // 1. Queued add_clip event — must arrive in the ext_call reply's
    //    events[] with the exact shape the editor pump consumes.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_call";
                r["ext_id"] = "demo";
                r["fn"] = "ve_demo_add_clip";
                r["args"] = "{}";
            },
            30000, &reply)) {
        return "ext_call ve_demo_add_clip got no reply";
    }
    Json::Value call;
    if (!parse(reply, &call)) {
        return "ext_call ve_demo_add_clip reply is malformed";
    }
    if (call.get("ok", false).asBool()) {
        const Json::Value& events = call["events"];
        if (!events.isArray()) {
            return "ext_call reply carries no events array";
        }
        const Json::Value* add = nullptr;
        for (const auto& e : events) {
            if (e.get("type", "").asString() == "add_clip") {
                add = &e;
            }
        }
        if (add == nullptr) {
            return "ve_demo_add_clip produced no add_clip event";
        }
        if (add->get("kind", "").asString() != "color" ||
            std::fabs(add->get("start", 0.0).asDouble() - 1.0) >
                1e-6 ||
            std::fabs(add->get("duration", 0.0).asDouble() - 2.5) >
                1e-6) {
            return "add_clip event fields are wrong";
        }
    }
    // ok:false tolerated — a demo packed before the export existed.

    // 2. Synchronous host channel — ve_call parked in the runtime is
    //    answered by OUR callback reply, proving the reverse channel
    //    end to end without the editor UI.
    if (!pipe.HasCallbackChannel()) {
        return {};   // worker spawned without the channel — tolerated
    }
    {
        std::atomic<bool> done{false};
        std::string call_reply;
        std::thread caller([&] {
            std::string r;
            static_cast<void>(round_trip(
                [&](Json::Value& req) {
                    req["kind"] = "ext_call";
                    req["ext_id"] = "demo";
                    req["fn"] = "ve_demo_hostcall";
                    req["args"] = "{}";
                },
                120000, &r));
            call_reply = std::move(r);
            done.store(true, std::memory_order_release);
        });
        bool saw_host_call = false;
        std::string failure;   // deferred — caller must join first
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(10);
        while (!done.load(std::memory_order_acquire) &&
               failure.empty() &&
               std::chrono::steady_clock::now() < deadline) {
            std::string line;
            if (!pipe.PopCallbackLine(&line)) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(5));
                continue;
            }
            Json::Value req;
            if (!parse(line, &req) ||
                req.get("kind", "").asString() != "host_call") {
                continue;
            }
            saw_host_call = true;
            if (req.get("name", "").asString() != "project.info") {
                failure = "host_call asked for an unexpected service";
                break;
            }
            Json::Value rsp(Json::objectValue);
            rsp["req_id"] = req["req_id"];
            rsp["ok"] = true;
            rsp["result"] = "{\"ok\":true,\"via\":\"smoke\"}";
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            if (!pipe.WriteCallbackLine(
                    Json::writeString(w, rsp))) {
                failure = "host_call reply write failed";
            }
        }
        if (!done.load(std::memory_order_acquire)) {
            // Unstick the parked RoundTrip before joining — a kill
            // breaks every pipe and fails the wait fast.
            pipe.KillWorker();
        }
        caller.join();
        if (!failure.empty()) {
            return failure;
        }
        if (!done.load(std::memory_order_acquire)) {
            return saw_host_call
                       ? "ve_call never returned after the host "
                         "reply — bridge broken"
                       : "ext_call hung with no host_call — demo "
                         "stale?";
        }
        if (call_reply.empty()) {
            return "ext_call ve_demo_hostcall got no reply";
        }
        Json::Value host;
        if (!parse(call_reply, &host)) {
            return "ve_demo_hostcall reply is malformed";
        }
        if (!host.get("ok", false).asBool()) {
            return {};   // symbol absent in this pack — tolerated
        }
        // The demo echoes ve_call's raw result: our editor reply must
        // be visible inside result_json.
        const std::string result =
            host.get("result_json", "").asString();
        if (result.find("\"via\":\"smoke\"") == std::string::npos) {
            return "ve_call did not return the editor's reply: " +
                   result;
        }
    }

    // Best-effort unload.
    reply.clear();
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        15000, &reply));
    return {};
#else
    return {};
#endif
}

// ext_xejslib — the .xejslib library chain end to end (tools/tz/
// xejslib.h, XSJOBuildX): when demo.xexrj carries
// mobjective/lib_*.xsjo members — spliced in by ztzmake --dep from
// libdemo.xejslib — ext_load must resolve the extension's own
// undefined libdemo:: symbols against them (the runtime meshes all
// of one extension's JITDylibs). Driving ve_demo_gain proves a
// mangled C++ symbol crosses module boundaries: result_json must
// carry libdemo's Version() string from the LIB module's rodata.
// Tolerated: no demo, no lib_* members (packed without the dep),
// missing runtime/JIT, or a stale pack where ve_demo_gain does not
// resolve. A present-but-wrong result is a hard failure.
std::string CheckExtXejslib() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    const std::filesystem::path ext_dir =
        bin_dir / "assets" / "extensions";
    const std::filesystem::path demo = ext_dir / "demo.xexrj";
    if (!std::filesystem::is_regular_file(demo, ec)) {
        return {};   // assets_icons owns the missing-package report
    }
    std::string open_err;
    auto arc = Tz::TzArchive::Open(Utf8Path(demo), &open_err);
    if (!arc) {
        return "cannot open demo.xexrj: " + open_err;
    }
    bool has_lib_member = false;
    for (const auto& name : arc->Names()) {
        if (name.rfind("mobjective/lib_", 0) == 0 &&
            name.size() > 5 &&
            name.compare(name.size() - 5, 5, ".xsjo") == 0) {
            has_lib_member = true;
            break;
        }
    }
    if (!has_lib_member) {
        return {};   // packed without a dep — nothing to prove
    }
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe pipe(WorkerPipeSpec{
        "VE.ExtensionsRuntime.exe", nullptr, "XER_VE_EXT_WORKER"});
    if (!pipe.Available()) {
        return "ext runtime spawn failed";
    }
    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  30000, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = Utf8Path(demo);
            },
            &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load)) {
        return "ext_load demo reply is malformed";
    }
    if (!load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable — ext_runtime reports it
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_call";
                r["ext_id"] = "demo";
                r["fn"] = "ve_demo_gain";
                r["args"] = "{}";
            },
            &reply)) {
        return "ext_call ve_demo_gain got no reply";
    }
    // Best-effort unload before evaluating the reply.
    std::string unreply;
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        &unreply));
    Json::Value call;
    if (!parse(reply, &call)) {
        return "ext_call ve_demo_gain reply is malformed";
    }
    if (!call.get("ok", false).asBool()) {
        return {};   // symbol absent in this pack — tolerated
    }
    const std::string result =
        call.get("result_json", "").asString();
    if (result.find("libdemo/1.0") == std::string::npos) {
        return "ve_demo_gain did not report libdemo's version "
               "(cross-module link failed?): " + result;
    }
    if (result.find("\"gain_q15\"") == std::string::npos ||
        result.find("\"out\"") == std::string::npos) {
        return "ve_demo_gain result lacks computed gain fields: " +
               result;
    }
    pipe.KillWorker();
    return {};
#else
    return {};
#endif
}

// ve_core_v3 — the ve_call service-table contract (sdk/ve.core.h):
//   1. VeServiceNames() coverage: every advertised namespace head is
//      present, VeServiceKnown() agrees with the list, and the
//      deliberate exclusions (app.quit, v2 aliases, garbage) stay
//      unadvertised.
//   2. The describe layer (project_store.cc) answers real project
//      JSON for a fabricated project — the same builders
//      DispatchExtHostCall serves editor-side, so the served shapes
//      are asserted here without a window.
//   3. Every Transition::Type round-trips
//      TransitionTypeToString/FromString — .xerproj files and the
//      "timeline.add_transition" arg vocabulary share that table.
//   4. Protocol end to end: the demo extension's ve_call reaches a
//      mini dispatcher over the REAL reverse channel (the same pump
//      the editor's UI tick runs). "project.info" answers with the
//      fabricated describe and "ve.services" with the real table —
//      the extension's result_json must carry both back verbatim.
// Tolerated on the pipe path: missing runtime/JIT, a stale demo
// pack, no channel (ext_bridge owns the transport complaints).
// The static parts always run.
std::string CheckVeCoreV3() {
    // 1. Service-table coverage + self-consistency.
    const auto& names = VeServiceNames();
    if (names.size() < 40) {
        return "VeServiceNames is suspiciously short (" +
               std::to_string(names.size()) + " entries)";
    }
    for (const char* required : {
             "ve.services", "app.info",
             "project.info", "project.new", "project.open",
             "project.save", "project.markers",
             "timeline.info", "timeline.tracks", "timeline.clips",
             "timeline.add_track", "timeline.remove_track",
             "timeline.rename_track", "timeline.set_track",
             "timeline.add_clip", "timeline.move_clip",
             "timeline.remove_clip", "timeline.trim_clip",
             "timeline.split_clip", "timeline.add_transition",
             "timeline.remove_transition",
             "transport.info", "transport.play", "transport.pause",
             "transport.stop", "transport.seek",
             "transport.frame_step", "transport.set_loop",
             "transport.set_rate",
             "selection.get", "selection.set",
             "media.list", "media.import", "media.refresh",
             "fx.list", "fx.add", "fx.remove",
             "export.start", "export.cancel", "export.status",
             "ui.status", "ui.open_panel",
             "edit.undo", "edit.redo",
             "timeline.set_clip", "timeline.set_keyframes",
             "ai.status", "ai.last", "ai.cancel",
             "ai.detect_beats", "ai.transcribe", "ai.find_objects",
             "ai.detect_silences", "ai.detect_scenes", "ai.loudness",
         }) {
        if (!VeServiceKnown(required)) {
            return std::string("service missing from VeServiceNames: ") +
                   required;
        }
    }
    for (const std::string& n : names) {
        if (!VeServiceKnown(n)) {
            return "VeServiceNames/VeServiceKnown disagree on " + n;
        }
        if (n != "ve.services" && n != "app.info" &&
            n.find('.') == std::string::npos) {
            return "unnamespaced service name " + n;
        }
    }
    // The deliberate exclusions: quit never ships, and the v2
    // aliases are still served but never advertised.
    if (VeServiceKnown("app.quit") || VeServiceKnown("project.clips") ||
        VeServiceKnown("status") || VeServiceKnown("bogus.service")) {
        return "VeServiceKnown advertised an unserved name";
    }

    // 2. Describe layer answers real project state.
    {
        Project project = MakeRenderProject(160, 90, 15);
        Marker marker;
        marker.time = 0.25;
        marker.name = "smoke-mark";
        project.markers.push_back(marker);
        const Json::Value info =
            VeDescribeProject(project, "demo.xerproj", true);
        if (info.get("width", 0).asInt() != 160 ||
            info.get("height", 0).asInt() != 90 ||
            info.get("fps_num", 0).asInt() != 15 ||
            info.get("markers", Json::UInt64(0)).asUInt64() != 1 ||
            !info.get("dirty", false).asBool()) {
            return "VeDescribeProject fields are wrong";
        }
        const Json::Value clips = VeDescribeClips(project);
        if (!clips["clips"].isArray() || clips["clips"].size() != 1 ||
            clips["clips"][0].get("kind", "").asString() != "bars" ||
            clips["clips"][0].get("id", "").asString() != "bars") {
            return "VeDescribeClips fields are wrong";
        }
        const Json::Value tracks = VeDescribeTracks(project);
        if (!tracks["tracks"].isArray() ||
            tracks["tracks"].size() != 2 ||
            tracks["tracks"][0].get("kind", "").asString() != "video" ||
            tracks["tracks"][1].get("kind", "").asString() != "audio") {
            return "VeDescribeTracks fields are wrong";
        }
        const Json::Value timeline =
            VeDescribeTimeline(project, 0.5, "bars");
        if (std::fabs(timeline.get("playhead", -1.0).asDouble() - 0.5) >
                1e-9 ||
            timeline.get("selected_clip", "").asString() != "bars" ||
            std::fabs(timeline.get("frame_seconds", -1.0).asDouble() -
                      1.0 / 15.0) > 1e-9) {
            return "VeDescribeTimeline fields are wrong";
        }
        const Json::Value markers = VeDescribeMarkers(project);
        if (!markers["markers"].isArray() ||
            markers["markers"].size() != 1 ||
            std::fabs(markers["markers"][0].get("time", -1.0)
                          .asDouble() - 0.25) > 1e-9) {
            return "VeDescribeMarkers fields are wrong";
        }
    }

    // 3. Transition wire names round-trip for every enum value, and
    //    garbage is rejected (add_transition's bad_type error).
    for (Transition::Type t : {Transition::Type::Crossfade,
                               Transition::Type::WipeLeft,
                               Transition::Type::SlideDown,
                               Transition::Type::DipToBlack,
                               Transition::Type::AdditiveDissolve,
                               Transition::Type::Script,
                               Transition::Type::Xfade}) {
        Transition::Type back = Transition::Type::Crossfade;
        const char* text = TransitionTypeToString(t);
        if (text == nullptr || *text == '\0' ||
            !TransitionTypeFromString(text, &back) || back != t) {
            return "transition wire-name round trip failed";
        }
    }
    {
        Transition::Type t;
        if (TransitionTypeFromString("not_a_type", &t)) {
            return "unknown transition name parsed";
        }
    }

#if defined(_WIN32)
    // 4. Protocol round trip + one REAL describe call through the
    //    reverse channel (the demo's ve_demo_hostcall /
    //    ve_demo_services exports drive the extension side).
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe& pipe = ExtRuntimePipe();
    if (!pipe.Available()) {
        return {};   // tolerated — spawn may be disabled
    }
    if (!pipe.HasCallbackChannel()) {
        return {};   // worker spawned without the channel — tolerated
    }
    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            int timeout_ms, std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  timeout_ms, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            15000, &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list reply malformed";
    }
    std::string demo_path;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
    }
    if (demo_path.empty()) {
        return {};   // no demo deployed — nothing to drive
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            60000, &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load) || !load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable or JS-only pack — tolerated
    }

    // The mini dispatcher answers host_call lines the way
    // DispatchExtHostCall does: "project.info" serves the fabricated
    // project's describe, "ve.services" the real table, anything
    // else the contract's {"ok":false,"error":"unknown_call"}.
    const Project demo_project = MakeRenderProject(160, 90, 15);
    const auto compact = [](const Json::Value& v) {
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        return Json::writeString(w, v);
    };
    const auto answer = [&](const Json::Value& req) -> std::string {
        Json::Value rsp(Json::objectValue);
        rsp["req_id"] = req["req_id"];
        rsp["ok"] = true;
        const std::string svc = req.get("name", "").asString();
        if (svc == "project.info") {
            Json::Value body =
                VeDescribeProject(demo_project, "", false);
            body["ok"] = true;
            rsp["result"] = compact(body);
        } else if (svc == "ve.services") {
            Json::Value body(Json::objectValue);
            body["ok"] = true;
            body["version"] = 3;
            Json::Value arr(Json::arrayValue);
            for (const std::string& s : names) arr.append(s);
            body["services"] = std::move(arr);
            rsp["result"] = compact(body);
        } else {
            rsp["result"] =
                "{\"ok\":false,\"error\":\"unknown_call\"}";
        }
        return compact(rsp);
    };

    // Two ext_calls on the caller thread — each blocks the runtime
    // inside ve_call until this pump services its host_call.
    std::atomic<bool> done{false};
    std::string host_reply, services_reply;
    std::thread caller([&] {
        std::string r;
        static_cast<void>(round_trip(
            [&](Json::Value& req) {
                req["kind"] = "ext_call";
                req["ext_id"] = "demo";
                req["fn"] = "ve_demo_hostcall";
                req["args"] = "{}";
            },
            120000, &r));
        host_reply = r;
        r.clear();
        static_cast<void>(round_trip(
            [&](Json::Value& req) {
                req["kind"] = "ext_call";
                req["ext_id"] = "demo";
                req["fn"] = "ve_demo_services";
                req["args"] = "{}";
            },
            120000, &r));
        services_reply = r;
        done.store(true, std::memory_order_release);
    });
    std::string failure;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done.load(std::memory_order_acquire) &&
           failure.empty() &&
           std::chrono::steady_clock::now() < deadline) {
        std::string line;
        if (!pipe.PopCallbackLine(&line)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        Json::Value req;
        if (!parse(line, &req) ||
            req.get("kind", "").asString() != "host_call") {
            continue;
        }
        if (!pipe.WriteCallbackLine(answer(req))) {
            failure = "host_call reply write failed";
        }
    }
    if (!done.load(std::memory_order_acquire)) {
        pipe.KillWorker();   // unstick the parked RoundTrip
    }
    caller.join();
    if (!failure.empty()) {
        return failure;
    }
    if (!done.load(std::memory_order_acquire)) {
        return "ext_calls hung — bridge pump starved";
    }

    // ve_demo_hostcall echoes the ve_call result verbatim: the
    // fabricated project's describe must be inside result_json.
    Json::Value host;
    if (host_reply.empty() || !parse(host_reply, &host)) {
        return "ve_demo_hostcall reply missing/malformed";
    }
    if (host.get("ok", false).asBool()) {
        const std::string result =
            host.get("result_json", "").asString();
        if (result.find("\"width\":160") == std::string::npos ||
            result.find("\"fps_num\":15") == std::string::npos) {
            return "ve_call did not return the describe payload: " +
                   result;
        }
    }
    // ok:false tolerated — stale pack without the export.

    // ve_demo_services echoes the ve.services table.
    Json::Value services;
    if (!services_reply.empty() && parse(services_reply, &services) &&
        services.get("ok", false).asBool()) {
        const std::string result =
            services.get("result_json", "").asString();
        if (result.find("\"version\":3") == std::string::npos ||
            result.find("timeline.info") == std::string::npos ||
            result.find("export.status") == std::string::npos) {
            return "ve.services answer lost the v3 table: " + result;
        }
    }
    // absent/malformed tolerated — ve_demo_services is new in the
    // demo; the transport already proved itself above.

    // Best-effort unload.
    reply.clear();
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        15000, &reply));
    return {};
#else
    return {};
#endif
}

// nn_probe.cc — prints the neural/AI backend matrix (which of
// OpenCV / ONNX Runtime / oneDNN / ggml / BLAS / MKL / CUDA / cuDNN
// this build carries and how each is wired). The report itself is
// informational and always passes; for every backend that IS
// compiled in a tiny real computation runs (cv::resize, Ort Env,
// dnnl::sum, a ggml add graph), so a broken link or an unresolvable
// delay-loaded DLL surfaces as a genuine FAIL instead of a silent
// "available" lie.
std::string CheckNnBackends() {
    const std::string report = NnBackendsReport();
    std::fprintf(stdout, "%s", report.c_str());
    std::fflush(stdout);
    for (const NnBackend& b : NnBackends()) {
        // Only backends this TU set actually links get exercised —
        // the report-only rows (BLAS runtime probe, MKL/CUDA/cuDNN
        // configure detections) have nothing to call into.
        if (!b.compiled_in) continue;
        const std::string id(b.id);
        if (id != "opencv" && id != "onnxruntime" && id != "dnnl" &&
            id != "ggml") {
            continue;
        }
        // Pre-probe the delay-import DLLs: the binder raises an
        // uncatchable exception on a missing/unloadable runtime, so a
        // broken toolchain package (stale DLL deps) would otherwise
        // crash the suite here. A runtime that cannot LoadLibrary is
        // "unavailable", not a test failure — report it loudly and
        // skip the computation.
        const std::string probe = NnBackendRuntimeProbe(b.id);
        if (!probe.empty()) {
            std::fprintf(stdout,
                         "  nn_backends: %s SKIP (runtime "
                         "unloadable) — %s\n",
                         id.c_str(), probe.c_str());
            std::fflush(stdout);
            continue;
        }
        const std::string err = NnBackendSelfTest(b.id);
        if (!err.empty()) {
            return id + " self-test failed: " + err;
        }
    }
    return {};
}

// ---- YA.* GPU-pipeline modules -------------------------------------

// YA.CUDADecode (src/gpu_video): prints the layered availability
// matrix, then proves the module end-to-end on whatever decode path
// this machine supports — CUDA hwaccel when the probe says yes, the
// internal software fallback otherwise. Passing on a GPU-less box IS
// the graceful-degradation contract: the module must stay loadable,
// probe honestly, and still decode.
std::string CheckCudaDecode() {
    const auto& probe = XER::GpuVideo::ProbeCudaDecode();
    std::fprintf(stdout, "%s", probe.Report().c_str());
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    // Encode a tiny h264 clip with the software encoder, then decode
    // it back through CudaVideoDecoder — the same Open/NextFrame path
    // a delegated media_decoder would drive.
    const auto clip = SmokeTempDir() / "smoke_cuda_decode.mp4";
    {
        std::error_code ec;
        std::filesystem::remove(clip, ec);
    }
    FFmpeg::EncodeVideoParams params;
    params.output_path = Utf8Path(clip);
    params.codec_name = "libx264";
    params.width = 64;
    params.height = 36;
    params.pixel_format = "bgra";
    params.fps_num = 15;
    std::string err;
    const auto provider = [](std::size_t index,
                             FFmpeg::VideoFrameInfo* f,
                             std::string*) -> bool {
        f->width = 64;
        f->height = 36;
        f->pixel_format = "bgra";
        f->pts = f->best_effort_timestamp =
            static_cast<std::int64_t>(index);
        f->line_sizes = {64 * 4, 0, 0, 0};
        f->data.assign(64 * 36 * 4,
                       static_cast<std::uint8_t>(index));
        return true;
    };
    if (!FFmpeg::EncodeVideoFrameStream(params, 8, provider, &err)) {
        return "could not encode fixture clip: " + err;
    }
    XER::GpuVideo::CudaVideoDecoder decoder;
    if (!decoder.Open(Utf8Path(clip), &err)) {
        return "CudaVideoDecoder.Open failed: " + err;
    }
    video::Frame frame;
    if (!decoder.NextFrame(&frame) || !frame.IsValid() ||
        frame.Width() != 64 || frame.Height() != 36) {
        return "CudaVideoDecoder produced no frame";
    }
    // The IMediaDecoder adapter (MakeGpuDecoder) must honor the same
    // contract — decode through it once to prove the delegation seam,
    // not just the module.
    MediaDecoderPtr delegated = MakeGpuDecoder();
    if (delegated == nullptr ||
        !delegated->Open(Utf8Path(clip), &err)) {
        return "MakeGpuDecoder delegation open failed: " + err;
    }
    std::vector<std::uint8_t> out_bgra;
    int out_w = 0, out_h = 0;
    if (!delegated->NextFrame(&out_bgra, &out_w, &out_h) ||
        out_w != 64 || out_h != 36 || out_bgra.empty()) {
        return "delegated decoder produced no frame";
    }
    std::fprintf(
        stdout,
        "  decode session: hwaccel=%s first_frame_surface=%s\n",
        decoder.ActiveHwaccelName().empty()
            ? "(software)"
            : decoder.ActiveHwaccelName().c_str(),
        decoder.LastFrameSurface() == XER::GpuVideo::FrameSurface::kCuda
            ? "cuda"
            : "cpu");
    return {};
}

// YA.CUDAEncode (src/gpu_video): availability matrix + the codec-name
// resolver contract export_engine relies on — software names pass
// through, "nvenc" aliases resolve to a concrete encoder when the
// device is usable, else fall back to libx264 with a note. Always
// passes (informational + graceful-degradation proof).
std::string CheckCudaEncode() {
    const auto& probe = XER::GpuVideo::ProbeCudaEncode();
    std::fprintf(stdout, "%s", probe.Report().c_str());
    const std::string passthrough =
        XER::GpuVideo::ResolveVideoCodecName("libx264");
    if (passthrough != "libx264") {
        return "software codec name was rewritten";
    }
    std::string note;
    const std::string resolved =
        XER::GpuVideo::ResolveVideoCodecName("nvenc", &note);
    if (resolved.empty()) {
        return "nvenc resolution returned empty";
    }
    if (!probe.Available() && resolved != "libx264") {
        return "nvenc resolved to '" + resolved +
               "' without a usable device";
    }
    std::fprintf(stdout, "  resolve: nvenc -> '%s' (%s)\n",
                 resolved.c_str(),
                 note.empty() ? "device usable" : note.c_str());
    return {};
}

// Recording fake Renderer — proves the YA.FrameRender transport seam:
// which draw call the service issues depends on the attached backend,
// and the pixel payload must arrive intact either way.
class RecordingRenderer final : public XER::UI::Rendering::Renderer {
public:
    int video_id_calls = 0;
    int video_pixels_calls = 0;
    int bgra_calls = 0;
    const void* last_pixels = nullptr;
    bool accept_id_draw = false;   // emulate a store-aware renderer

    void BeginFrame(XER::UI::Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(XER::UI::Layout::Rect, XER::UI::Theme::Color,
                  float) override {}
    void StrokeRect(XER::UI::Layout::Rect, XER::UI::Theme::Color,
                    float, float) override {}
    void DrawLine(XER::UI::Layout::Point, XER::UI::Layout::Point,
                  XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, XER::UI::Layout::Rect,
                  XER::UI::Rendering::TextStyle) override {}
    XER::UI::Layout::Size MeasureText(
        std::string_view,
        XER::UI::Rendering::TextStyle) const override {
        return {};
    }
    bool DrawVideoFrame(std::uint32_t, XER::UI::Layout::Rect, float,
                        float) override {
        ++video_id_calls;
        return accept_id_draw;
    }
    bool DrawVideoPixels(const void* bgra, std::uint32_t, int, int,
                         XER::UI::Layout::Rect, float,
                         float) override {
        ++video_pixels_calls;
        last_pixels = bgra;
        return true;
    }
    bool DrawFrameBgra(const void* bgra, int, int,
                       XER::UI::Layout::Rect, float) override {
        ++bgra_calls;
        last_pixels = bgra;
        return true;
    }
};

// YA.FrameRender (src/frame_render): availability matrix + a REAL
// CPU-path render test (BGRA/RGBA/NV12 normalize + Present through a
// recording renderer), plus the RenderStore seam when this build
// carries it. Always passes on the availability side.
std::string CheckFrameRender() {
    const auto probe = XER::FrameRender::ProbeFrameRender();
    std::fprintf(stdout, "%s", probe.Report().c_str());

    video::Frame bgra(4, 2, video::PixelFormat::BGRA8);
    std::memset(bgra.Data(), 0, bgra.DataSize());
    bgra.Data()[0] = 10;
    bgra.Data()[1] = 20;
    bgra.Data()[2] = 30;
    bgra.Data()[3] = 255;
    std::vector<std::uint8_t> out;
    int w = 0, h = 0;
    if (!XER::FrameRender::Service::Normalize(bgra, &out, &w, &h) ||
        w != 4 || h != 2 || out.size() != 32 || out[0] != 10 ||
        out[1] != 20 || out[2] != 30) {
        return "BGRA normalize failed";
    }
    video::Frame rgba(2, 2, video::PixelFormat::RGBA8);
    rgba.Data()[0] = 30;
    rgba.Data()[1] = 20;
    rgba.Data()[2] = 10;
    rgba.Data()[3] = 255;
    if (!XER::FrameRender::Service::Normalize(rgba, &out, &w, &h) ||
        out[0] != 10 || out[2] != 30) {
        return "RGBA swizzle failed";
    }
    // Neutral NV12 (Y=U=V=128) must land as mid-gray in BGRA.
    video::Frame nv12(4, 4, video::PixelFormat::NV12);
    std::memset(nv12.Data(), 128, nv12.DataSize());
    if (!XER::FrameRender::Service::Normalize(nv12, &out, &w, &h) ||
        out.empty()) {
        return "NV12 normalize failed";
    }
    if (std::abs(static_cast<int>(out[2]) - 128) > 4 ||
        std::abs(static_cast<int>(out[1]) - 128) > 4 ||
        std::abs(static_cast<int>(out[0]) - 128) > 4) {
        return "NV12 neutral gray drifted";
    }

    // Transport seam: with no store attached the pixel path must be
    // chosen; the same call routes through the store slot when one is
    // attached and the renderer accepts the id draw.
    XER::FrameRender::Service service;
    if (!service.Init({})) {
        return "frame-render service init failed";
    }
    RecordingRenderer rec;
    const XER::UI::Layout::Rect dst{0.0f, 0.0f, 4.0f, 2.0f};
    if (!service.Present(rec, bgra, dst, 1.0f) ||
        service.LastBackend() != XER::FrameRender::Backend::kCpu ||
        rec.video_pixels_calls + rec.bgra_calls == 0) {
        return "CPU present path not taken";
    }

    // Store seam — a throwaway segment proves the write+id-draw path
    // (production attaches the UI host's xer.ui.res.<pid> store).
    // render_store_compiled is a runtime flag because the compile-time
    // define lives on the module's objects, not its consumers.
    if (probe.render_store_compiled) {
        auto store = XER::UI::Rendering::RenderStore::Create(
            "xer.ve.smoke.frame_render", 4u * 1024u * 1024u);
        if (store != nullptr) {
            XER::FrameRender::Service stored;
            XER::FrameRender::Options opts;
            opts.store = store.get();
            if (!stored.Init(opts)) {
                return "store-backed init failed";
            }
            RecordingRenderer rec2;
            rec2.accept_id_draw = true;
            if (!stored.Present(rec2, bgra, dst, 1.0f) ||
                stored.LastBackend() !=
                    XER::FrameRender::Backend::kRenderStore ||
                rec2.video_id_calls != 1) {
                return "render_store path not taken";
            }
            const auto view = store->Read(stored.VideoSlot());
            if (view.data == nullptr || view.info.width != 4 ||
                view.info.height != 2 || view.info.payload < 32) {
                return "store slot did not receive the frame";
            }
        }
    }
    std::fprintf(stdout,
                 "  frame_render: cpu path verified%s\n",
                 probe.render_store_compiled ? " (+ store seam)"
                                             : "");
    return {};
}

// YA.AIFindObject (src/content/ai/find_object) — the "AI: Find
// Objects" backend matrix. Informational like nn_backends: prints
// whether ONNX Runtime / OpenCV DNN are linked+loadable and whether a
// detection model is installed. What MUST hold regardless of the
// environment: both backend rows are reported, a missing model path
// fails Load() with a reason instead of crashing, and Detect() on an
// unloaded detector returns empty. When a model IS installed the
// check goes one step further and Detect()s a synthetic BGRA frame —
// a real inference path exercised end to end.
std::string CheckAiFindObject() {
    namespace FindObj = XER::AI::FindObject;

    // ---- availability matrix (always printed) ----
    const std::vector<FindObj::BackendInfo> backends =
        FindObj::Backends();
    std::fprintf(stdout, "  ai_find_object backends:\n%s",
                 FindObj::BackendReport().c_str());
    const std::string model = Ai::FindObjectModel();
    std::fprintf(stdout, "  ai_find_object model: %s\n",
                 model.empty() ? "<none>" : model.c_str());
    std::fflush(stdout);
    if (backends.size() != 2) {
        return "Backends() must report both rows (onnxruntime, "
               "opencv_dnn)";
    }

    // ---- unavailable paths must degrade, never crash ----
    FindObj::Detector detector;
    if (detector.IsLoaded() || !detector.BackendId().empty()) {
        return "fresh Detector reports loaded";
    }
    std::string err;
    if (detector.Load("definitely/missing/model.onnx", "", "", &err)) {
        return "Load() accepted a nonexistent model";
    }
    if (err.empty()) {
        return "Load() failed a missing model without a reason";
    }
    // Synthetic BGRA8 frame (160x120 opaque gray) — Detect on an
    // unloaded detector is a documented empty result.
    std::vector<std::uint8_t> bgra(160 * 120 * 4, 0x80);
    for (std::size_t p = 3; p < bgra.size(); p += 4) bgra[p] = 0xFF;
    if (!detector.Detect(bgra.data(), 160, 120, 160 * 4).empty()) {
        return "unloaded Detector returned detections";
    }

    // ---- real-model path: only when the user installed one ----
    if (model.empty() || FindObj::ActiveBackendId().empty()) {
        return {};   // nothing to run inference on — graceful skip
    }
    if (!detector.Load(model, Ai::FindObjectLabels(model), "", &err)) {
        return "installed model failed to load: " + err;
    }
    // A uniform frame may legitimately produce zero detections — the
    // contract is "returns a vector, does not crash".
    static_cast<void>(detector.Detect(bgra.data(), 160, 120, 160 * 4));
    std::fprintf(stdout,
                 "  ai_find_object inference ran on backend '%s'\n",
                 detector.BackendId().c_str());
    std::fflush(stdout);
    return {};
}

// AI tag layer (ai_tags.h + Clip::tags persistence). Verifies the
// graceful-degradation calls (no model / un-decodable clip fail with
// reasons, never crash) and the project-store round trip that keeps
// ClipTag ranges on the clip.
std::string CheckAiTags() {
    // ---- graceful-degradation contract ----
    Clip clip;
    clip.id = "tag-scan";
    clip.source_path = "definitely/missing/media.mp4";
    clip.duration = 5.0;
    clip.in_point = 0.0;
    std::vector<ClipTag> tags;
    std::string err;
    if (Ai::ScanClipForObjects(clip, "", "", Ai::TagScanParams{},
                               nullptr, nullptr, &tags, &err)) {
        return "ScanClipForObjects succeeded with no model";
    }
    if (err.empty() || !tags.empty()) {
        return "no-model scan did not report a reason / left tags";
    }
    Clip generated = MakeGeneratedClip("gen", 0, 0.0, 1.0,
                                       SourceKind::ColorBars,
                                       "smpte75");
    if (Ai::ScanClipForObjects(generated, "x.onnx", "",
                               Ai::TagScanParams{}, nullptr, nullptr,
                               &tags, &err)) {
        return "ScanClipForObjects scanned a generated clip";
    }

    // ---- Clip::tags serialization round trip ----
    Project project;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip tagged;
    tagged.id = "tagged";
    tagged.source_path = "tagged.mp4";   // need not exist for the store
    tagged.track = 0;
    tagged.timeline_start = 0.0;
    tagged.in_point = 4.0;
    tagged.duration = 8.0;
    ClipTag t0;
    t0.start = 5.0;
    t0.end = 7.5;
    t0.label = "person";
    t0.confidence = 0.83f;
    ClipTag t1;
    t1.start = 9.0;
    t1.end = 10.25;
    t1.label = "car";
    t1.confidence = 0.42f;
    tagged.tags = {t0, t1};
    project.clips.push_back(tagged);

    const std::string path =
        Utf8Path(SmokeTempDir() / "smoke_ai_tags.xerproj");
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    const Clip* back = FindClip(loaded, "tagged");
    if (back == nullptr || back->tags.size() != 2) {
        return "clip tags did not survive the round trip";
    }
    const ClipTag& a = back->tags[0];
    const ClipTag& b = back->tags[1];
    if (a.start != 5.0 || a.end != 7.5 || a.label != "person" ||
        std::abs(a.confidence - 0.83f) > 1e-6f ||
        b.start != 9.0 || b.end != 10.25 || b.label != "car" ||
        std::abs(b.confidence - 0.42f) > 1e-6f) {
        return "tag fields did not survive the round trip";
    }
    // And the inverse: a tag-free clip stays tag-free.
    Clip plain = MakeGeneratedClip("plain", 0, 20.0, 1.0,
                                   SourceKind::ColorMatte, "#112233");
    project.clips.push_back(plain);
    if (!ProjectStore::Save(project, path, &err) ||
        !ProjectStore::Load(path, &loaded, &err)) {
        return "second round trip failed: " + err;
    }
    if (const Clip* p = FindClip(loaded, "plain");
        p == nullptr || !p->tags.empty()) {
        return "tag-free clip gained tags in the round trip";
    }
    return {};
}

// Smart Reframe (ai/smart_reframe.h + compositor warp integration +
// Clip::reframe persistence). Model-level, no real media needed:
//   * ReframeWindowAt math — aspect-locked window, clamped inside
//     the frame, follows the tracked subject, obeys the smoothing
//     parameter, snaps at flagged scene boundaries, center-crops on
//     an empty track;
//   * the registered builtin:xer.fx.smart_reframe applied end to
//     end — a bars clip whose tracked subject sits on the right half
//     must render a canvas built from that half, not a center crop;
//   * Clip::reframe samples round-trip through .xerproj.
std::string CheckSmartReframe() {
    constexpr double kRatio = 9.0 / 16.0;

    // ---- crop-window math ----
    // Empty track on a 200x100 frame: centered 9:16 window
    // (56.25 x 100) — the documented center-crop fallback.
    const Ai::ReframeWindow empty_win = Ai::ReframeWindowAt(
        ClipReframe{}, 0.0, 200, 100, kRatio, 0.5);
    if (std::abs(empty_win.w - 56.25) > 0.01 ||
        std::abs(empty_win.h - 100.0) > 0.01 ||
        std::abs(empty_win.x - (200.0 - 56.25) * 0.5) > 0.01 ||
        empty_win.y != 0.0) {
        return "empty track did not produce the centered 9:16 window";
    }
    // A subject at cx=0.9: the window must still CONTAIN the subject
    // while staying clamped inside the frame.
    ClipReframe side_track;
    ReframeSample side;
    side.t = 0.0;
    side.cx = 0.9f;
    side.cy = 0.5f;
    side.w = 0.2f;
    side.h = 0.5f;
    side.confidence = 0.9f;
    side_track.samples.push_back(side);
    const Ai::ReframeWindow side_win = Ai::ReframeWindowAt(
        side_track, 0.0, 200, 100, kRatio, 0.5);
    const double subject_x = 0.9 * 200.0;
    if (side_win.x < -0.01 || side_win.y < -0.01 ||
        side_win.x + side_win.w > 200.01 ||
        side_win.y + side_win.h > 100.01 ||
        subject_x < side_win.x - 0.01 ||
        subject_x > side_win.x + side_win.w + 0.01) {
        return "subject-side window off-frame or misses the subject";
    }
    // Interpolation: at t=0.5 between samples at t=0 (cx=0.2) and
    // t=1 (cx=0.8), smoothing=0 lerps to cx≈0.5.
    ClipReframe jump;
    ReframeSample a, b;
    a.t = 0.0;
    a.cx = 0.2f;
    a.cy = 0.5f;
    b.t = 1.0;
    b.cx = 0.8f;
    b.cy = 0.5f;
    jump.samples = {a, b};
    const double mid_x =
        Ai::ReframeWindowAt(jump, 0.5, 200, 100, kRatio, 0.0).x;
    if (std::abs(mid_x - (100.0 - 56.25 * 0.5)) > 0.6) {
        return "interpolated window did not track the lerp";
    }
    // Smoothing: the same jump at t=1 — smoothing=0 follows raw,
    // smoothing=1 must restrain it (EMA horizon + velocity cap).
    const double raw_x = Ai::ReframeWindowAt(jump, 1.0, 200, 100,
                                             kRatio, 0.0)
                             .x;
    const double damped_x = Ai::ReframeWindowAt(jump, 1.0, 200, 100,
                                                kRatio, 1.0)
                                .x;
    if (damped_x >= raw_x - 1.0) {
        return "max smoothing did not restrain the window jump";
    }
    // Scene-boundary flag: the smoother snaps to the post-cut sample.
    jump.samples[1].scene_boundary = true;
    const double snapped_x = Ai::ReframeWindowAt(jump, 1.0, 200, 100,
                                                 kRatio, 1.0)
                                 .x;
    if (snapped_x < raw_x - 0.6) {
        return "scene-boundary sample did not snap the window";
    }
    // Degenerate inputs degrade, never crash.
    if (Ai::ReframeWindowAt(jump, 0.5, 0, 0, kRatio, 0.5).w != 0.0 ||
        Ai::ReframeWindowAt(jump, 0.5, 200, 100, 0.0, 0.5).w != 0.0) {
        return "degenerate dims/ratio must yield an empty window";
    }

    // ---- scan graceful-degradation contract ----
    std::string err;
    Clip clip;
    clip.id = "reframe-scan";
    clip.source_path = "definitely/missing/media.mp4";
    clip.duration = 5.0;
    ClipReframe scanned;
    if (Ai::ScanClipSubject(clip, "", "", Ai::ReframeScanParams{},
                            nullptr, nullptr, &scanned, &err)) {
        return "ScanClipSubject succeeded with no model";
    }
    if (err.empty() || !scanned.samples.empty()) {
        return "no-model scan did not report a reason / left samples";
    }
    Clip generated = MakeGeneratedClip("gen", 0, 0.0, 1.0,
                                       SourceKind::ColorBars,
                                       "smpte75");
    if (Ai::ScanClipSubject(generated, "x.onnx", "",
                            Ai::ReframeScanParams{}, nullptr, nullptr,
                            &scanned, &err)) {
        return "ScanClipSubject scanned a generated clip";
    }

    // ---- registered descriptor ----
    const PluginDesc* desc =
        PluginHost::ForEffectType("builtin:xer.fx.smart_reframe");
    if (desc == nullptr || !desc->builtin ||
        desc->id != Ai::kSmartReframeId) {
        return "builtin:xer.fx.smart_reframe is not registered";
    }
    if (desc->params.size() != 2 ||
        std::string(desc->params[0].name) != "target_ratio" ||
        std::string(desc->params[1].name) != "smoothing") {
        return "smart_reframe param table is not {target_ratio, "
               "smoothing}";
    }

    // ---- end-to-end render: tracked subject at the right side ----
    // 200x100 canvas, bars clip (7 SMPTE columns ≈28.6px), target
    // ratio 0.5 (1:2) → crop is the 50x100 window centered on the
    // tracked subject at cx=0.75 → source x∈[125,175]. The canvas
    // must then be built from that right-half region: center pixel
    // maps to source x≈150 (bar 5 = red), left edge to x≈125
    // (bar 4 = magenta) — a center crop would show x≈75/100 (bars
    // 2/3 = cyan/green) instead.
    {
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip bars_clip = MakeGeneratedClip("bars", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        bars_clip.effects.push_back(
            {"builtin:xer.fx.smart_reframe", {0.5f, 0.5f}});
        ReframeSample key;
        key.t = 0.0;
        key.cx = 0.75f;
        key.cy = 0.5f;
        key.confidence = 0.9f;
        bars_clip.reframe.samples.push_back(key);
        project.clips.push_back(bars_clip);

        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        const bool rendered = compositor.RenderFrameAt(0.0, &bgra, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const auto px = [&bgra](int x, int y) {
            const std::uint8_t* p =
                bgra.data() +
                (static_cast<std::size_t>(y) * 200 + x) * 4;
            return std::array<int, 3>{p[0], p[1], p[2]};   // B,G,R
        };
        const auto mid = px(100, 50);    // source x≈150 → bar 5 red
        const auto left = px(10, 50);    // source x≈127 → bar 4 magenta
        const auto right = px(190, 50);  // source x≈172 → bar 6 blue
        if (!(mid[0] < 60 && mid[1] < 60 && mid[2] > 150)) {
            return "reframed canvas center is not the tracked "
                   "subject's region (expected red)";
        }
        if (!(left[0] > 150 && left[1] < 60 && left[2] > 150)) {
            return "reframed left edge is not the source's right-half "
                   "bar (expected magenta)";
        }
        if (!(right[0] > 150 && right[1] < 60 && right[2] < 60)) {
            return "reframed right edge is not the source's far-right "
                   "bar (expected blue)";
        }
    }
    // And the same clip WITHOUT the track center-crops: canvas center
    // maps to source x=100 (bar 3 = green).
    {
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip bars_clip = MakeGeneratedClip("bars", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        bars_clip.effects.push_back(
            {"builtin:xer.fx.smart_reframe", {0.5f, 0.5f}});
        project.clips.push_back(bars_clip);
        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        const bool rendered = compositor.RenderFrameAt(0.0, &bgra, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const std::uint8_t* p =
            bgra.data() + (static_cast<std::size_t>(50) * 200 + 100) * 4;
        // source x=100 → bar 3 green {B=0,G=191,R=0}
        if (!(p[0] < 60 && p[1] > 150 && p[2] < 60)) {
            return "trackless smart-reframe did not center-crop "
                   "(expected green at canvas center)";
        }
    }

    // ---- Clip::reframe serialization round trip ----
    {
        Project project;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip tracked;
        tracked.id = "reframed";
        tracked.source_path = "reframed.mp4";  // need not exist
        tracked.track = 0;
        tracked.timeline_start = 0.0;
        tracked.duration = 4.0;
        ReframeSample s0;
        s0.t = 0.5;
        s0.cx = 0.3f;
        s0.cy = 0.6f;
        s0.w = 0.2f;
        s0.h = 0.4f;
        s0.confidence = 0.77f;
        ReframeSample s1;
        s1.t = 1.0;
        s1.cx = 0.7f;
        s1.cy = 0.4f;
        s1.w = 0.3f;
        s1.h = 0.2f;
        s1.confidence = 0.55f;
        s1.scene_boundary = true;
        tracked.reframe.samples = {s0, s1};
        project.clips.push_back(tracked);

        const std::string path =
            Utf8Path(SmokeTempDir() / "smoke_reframe.xerproj");
        if (!ProjectStore::Save(project, path, &err)) {
            return "Save failed: " + err;
        }
        Project loaded;
        if (!ProjectStore::Load(path, &loaded, &err)) {
            return "Load failed: " + err;
        }
        const Clip* back = FindClip(loaded, "reframed");
        if (back == nullptr || back->reframe.samples.size() != 2) {
            return "reframe samples did not survive the round trip";
        }
        const ReframeSample& r0 = back->reframe.samples[0];
        const ReframeSample& r1 = back->reframe.samples[1];
        if (r0.t != 0.5 || std::abs(r0.cx - 0.3f) > 1e-6f ||
            std::abs(r0.cy - 0.6f) > 1e-6f ||
            std::abs(r0.w - 0.2f) > 1e-6f ||
            std::abs(r0.h - 0.4f) > 1e-6f ||
            std::abs(r0.confidence - 0.77f) > 1e-6f ||
            r0.scene_boundary || r1.t != 1.0 ||
            std::abs(r1.cx - 0.7f) > 1e-6f || !r1.scene_boundary) {
            return "reframe fields did not survive the round trip";
        }
    }
    return {};
}

// "Track object → pin content" (ai/object_track.h): the reusable
// ClipTrack on Clip::object_track plus its two pin consumers — baked
// PosX/PosY keyframes for a generated title (BakePinKeys) and the
// live-reference xer.fx.track_mosaic censor resolved from the track
// in the compositor. Everything here is deterministic model/render
// math — the RF-DETR scan itself shares the graceful-degradation
// contract with smart_reframe (no model => reason, never crash).
std::string CheckObjectTrack() {
    std::string err;

    // ---- TrackBoxAt: smoothed box interpolation ----
    ClipTrack track;
    ReframeSample ta, tb;
    ta.t = 0.0;
    ta.cx = 0.2f;
    ta.cy = 0.4f;
    ta.w = 0.10f;
    ta.h = 0.20f;
    tb.t = 1.0;
    tb.cx = 0.8f;
    tb.cy = 0.4f;
    tb.w = 0.20f;
    tb.h = 0.40f;
    track.samples = {ta, tb};
    {
        // Empty track -> degenerate box (w <= 0 => "nothing tracked").
        const Ai::TrackBox empty = Ai::TrackBoxAt(ClipTrack{}, 0.5, 0.0);
        if (empty.w > 0.0 || empty.h > 0.0) {
            return "empty object track produced a non-empty box";
        }
        // Smoothing 0 follows raw centers at sample times exactly.
        const Ai::TrackBox at0 = Ai::TrackBoxAt(track, 0.0, 0.0);
        const Ai::TrackBox at1 = Ai::TrackBoxAt(track, 1.0, 0.0);
        if (std::abs(at0.cx - 0.2) > 1e-4 || at1.cx < 0.6) {
            return "TrackBoxAt did not follow the raw centers";
        }
        // Mid-time: center lerps to ~0.5, size lerps to the mean.
        const Ai::TrackBox mid = Ai::TrackBoxAt(track, 0.5, 0.0);
        if (std::abs(mid.cx - 0.5) > 0.05 ||
            std::abs(mid.w - 0.15) > 0.02 ||
            std::abs(mid.h - 0.30) > 0.04) {
            return "TrackBoxAt midpoint did not lerp center and size";
        }
        // End holds beyond the last sample.
        const Ai::TrackBox late = Ai::TrackBoxAt(track, 5.0, 0.0);
        if (std::abs(late.cx - 0.8) > 1e-4 ||
            std::abs(late.w - 0.20) > 1e-4) {
            return "TrackBoxAt did not hold the end of the track";
        }
        // Max smoothing restrains the 0.6-fraction jump at t=1
        // (EMA horizon + velocity cap — same curve as Smart Reframe).
        const Ai::TrackBox damped = Ai::TrackBoxAt(track, 1.0, 1.0);
        if (damped.cx >= at1.cx - 0.05) {
            return "TrackBoxAt smoothing did not restrain the jump";
        }
    }

    // ---- BakePinKeys: track -> PosX/PosY keyframes ----
    // Identity-transform source on a same-size canvas maps source
    // fractions to canvas fractions 1:1, so expected key values are
    // exact. TopCenter pin: the title's bottom-center rests margin
    // above the tracked box top.
    {
        Clip tracked;
        tracked.id = "tracked";
        tracked.source_path = "media.mp4";
        tracked.track = 0;
        tracked.timeline_start = 0.0;
        tracked.duration = 1.0;
        tracked.in_point = 0.0;
        tracked.playback_rate = 1.0;
        ReframeSample s0, s1;
        s0.t = 0.0;
        s0.cx = 0.2f;
        s0.cy = 0.6f;
        s0.w = 0.2f;
        s0.h = 0.2f;   // box top at cy - h/2 = 0.5
        s1.t = 1.0;
        s1.cx = 0.8f;
        s1.cy = 0.6f;
        s1.w = 0.2f;
        s1.h = 0.2f;
        tracked.object_track.samples = {s0, s1};
        Ai::PinGeometry geom;
        geom.src_w = 200;
        geom.src_h = 100;
        geom.canvas_w = 200;
        geom.canvas_h = 100;
        geom.pin_w = 40.0;   // title frame, canvas px at scale 1
        geom.pin_h = 20.0;
        geom.pin_scale = 1.0;
        geom.margin = 0.02;
        Ai::PinKeys keys;
        if (!Ai::BakePinKeys(tracked, Ai::PinAnchor::TopCenter, geom,
                             0.0, &keys, &err)) {
            return "BakePinKeys failed: " + err;
        }
        if (keys.pos_x.size() != 2 || keys.pos_y.size() != 2) {
            return "BakePinKeys produced the wrong key count";
        }
        // Anchor point: (cx, cy - h/2) = (0.2, 0.5); margin lifts ay
        // to 0.48. Pinned top-left ox = ax - 0.5*pin_w/canvas_w =
        // 0.2 - 0.1 = 0.1; oy = 0.48 - 1.0*pin_h/canvas_h = 0.28.
        const double x0 = keys.pos_x[0].value;
        const double y0 = keys.pos_y[0].value;
        const double x1 = keys.pos_x[1].value;
        const double y1 = keys.pos_y[1].value;
        if (std::abs(keys.pos_x[0].time - 0.0) > 1e-6 ||
            std::abs(keys.pos_x[1].time - 1.0) > 1e-6 ||
            std::abs(x0 - 0.1) > 1e-4 || std::abs(y0 - 0.28) > 1e-4 ||
            std::abs(x1 - 0.7) > 1e-4 || std::abs(y1 - 0.28) > 1e-4) {
            return "BakePinKeys landed the wrong pin coordinates";
        }
        // The baked lists are ordinary clip keyframes: evaluate the
        // synthetic title clip mid-span and expect the lerp midpoint.
        Clip title;
        title.id = "pin-title";
        title.track = 0;
        title.duration = 1.0;
        title.keyframes[KeyProp::PosX] = keys.pos_x;
        title.keyframes[KeyProp::PosY] = keys.pos_y;
        if (std::abs(EvaluateClipProperty(title, KeyProp::PosX, 0.5,
                                          0.0f) -
                     0.4f) > 1e-4f ||
            std::abs(EvaluateClipProperty(title, KeyProp::PosY, 0.5,
                                          0.0f) -
                     0.28f) > 1e-4f) {
            return "baked pin keys did not evaluate to the midpoint";
        }
        // in_point shifts keys into the clip-local domain: a track
        // sample at source t=5 on a clip consuming [5,6] bakes u=0.
        Clip trimmed = tracked;
        trimmed.in_point = 5.0;
        ReframeSample s5, s6;
        s5 = s0;
        s5.t = 5.0;
        s6 = s1;
        s6.t = 6.0;
        trimmed.object_track.samples = {s5, s6};
        Ai::PinKeys keys2;
        if (!Ai::BakePinKeys(trimmed, Ai::PinAnchor::TopCenter, geom,
                             0.0, &keys2, &err) ||
            keys2.pos_x.size() != 2 ||
            std::abs(keys2.pos_x[0].time - 0.0) > 1e-6 ||
            std::abs(keys2.pos_x[1].time - 1.0) > 1e-6) {
            return "in_point did not map samples to clip-local keys";
        }
        // Samples outside the consumed span are dropped; an entirely
        // out-of-span / empty track is a clean failure.
        trimmed.object_track.samples[0].t = 100.0;
        trimmed.object_track.samples[1].t = 200.0;
        if (Ai::BakePinKeys(trimmed, Ai::PinAnchor::TopCenter, geom,
                            0.0, &keys2, &err)) {
            return "out-of-span track baked keys anyway";
        }
        Clip empty;
        empty.id = "empty";
        empty.duration = 1.0;
        if (Ai::BakePinKeys(empty, Ai::PinAnchor::TopCenter, geom,
                            0.0, &keys2, &err)) {
            return "BakePinKeys baked from an empty track";
        }
        Ai::PinGeometry bad = geom;
        bad.canvas_w = 0;
        if (Ai::BakePinKeys(tracked, Ai::PinAnchor::TopCenter, bad,
                            0.0, &keys2, &err)) {
            return "BakePinKeys accepted degenerate geometry";
        }
    }

    // ---- scan graceful-degradation contract ----
    {
        Clip clip;
        clip.id = "track-scan";
        clip.source_path = "definitely/missing/media.mp4";
        clip.duration = 5.0;
        ClipTrack scanned;
        if (Ai::ScanClipTrack(clip, "", "", Ai::ReframeScanParams{},
                              "", nullptr, nullptr, &scanned, &err)) {
            return "ScanClipTrack succeeded with no model";
        }
        if (err.empty() || !scanned.samples.empty()) {
            return "no-model track scan did not report a reason";
        }
        Clip generated = MakeGeneratedClip("gen2", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        if (Ai::ScanClipTrack(generated, "x.onnx", "",
                              Ai::ReframeScanParams{}, "", nullptr,
                              nullptr, &scanned, &err)) {
            return "ScanClipTrack scanned a generated clip";
        }
    }

    // ---- registered descriptor ----
    {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:xer.fx.track_mosaic");
        if (desc == nullptr || !desc->builtin ||
            desc->id != Ai::kTrackMosaicId) {
            return "builtin:xer.fx.track_mosaic is not registered";
        }
        if (desc->params.size() != 3 ||
            std::string(desc->params[0].name) != "block" ||
            std::string(desc->params[1].name) != "padding" ||
            std::string(desc->params[2].name) != "smoothing") {
            return "track_mosaic param table is not {block, padding, "
                   "smoothing}";
        }
    }

    // ---- end-to-end render: the mosaic pixelates the tracked box ----
    // 200x100 bars (each ~28.6px). A track box at cx=0.25, w=0.10
    // covers x in [40,60] — spanning the yellow->cyan boundary at
    // x~=57. With block 128 the whole region collapses to one averaged
    // color: two pixels that would differ unfixed (40 vs 58) become
    // equal, and a pixel outside the box keeps its bar color.
    {
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip bars_clip = MakeGeneratedClip("bars-m", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        bars_clip.effects.push_back(
            {"builtin:xer.fx.track_mosaic", {128.0f, 0.0f, 0.0f}});
        ReframeSample s;
        s.t = 0.0;
        s.cx = 0.25f;
        s.cy = 0.5f;
        s.w = 0.10f;
        s.h = 1.0f;
        s.confidence = 0.9f;
        bars_clip.object_track.samples.push_back(s);
        project.clips.push_back(bars_clip);
        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        const bool rendered = compositor.RenderFrameAt(0.0, &bgra, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const auto px = [&bgra](int x, int y) {
            const std::uint8_t* p =
                bgra.data() +
                (static_cast<std::size_t>(y) * 200 + x) * 4;
            return std::array<int, 4>{p[0], p[1], p[2], p[3]};
        };
        const auto a = px(42, 50);   // inside box, would be yellow
        const auto b = px(58, 50);   // inside box, would be cyan
        const auto outside = px(70, 50);  // beyond the box: cyan bar
        if (a != b) {
            return "tracked mosaic did not flatten the box to one "
                   "color";
        }
        // The merged color mixes yellow (B=0) with cyan (B~191), so
        // its blue channel lands strictly between them.
        if (a[0] <= 0 || a[0] >= 190) {
            return "tracked mosaic left the box unpixelated";
        }
        // Outside pixels keep their source color (cyan BGRA-ish):
        // cyan ~{191,191,0} in BGR.
        if (!(outside[0] > 150 && outside[1] > 150 &&
              outside[2] < 60)) {
            return "tracked mosaic pixelated outside the tracked box";
        }
    }
    // And the same clip WITHOUT the effect stays unpixed — the two
    // boundary pixels then differ (yellow vs cyan).
    {
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip bars_clip = MakeGeneratedClip("bars-m2", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        ReframeSample s;
        s.t = 0.0;
        s.cx = 0.25f;
        s.cy = 0.5f;
        s.w = 0.10f;
        s.h = 1.0f;
        bars_clip.object_track.samples.push_back(s);
        project.clips.push_back(bars_clip);
        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> bgra;
        const bool rendered = compositor.RenderFrameAt(0.0, &bgra, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const auto px = [&bgra](int x, int y) {
            const std::uint8_t* p =
                bgra.data() +
                (static_cast<std::size_t>(y) * 200 + x) * 4;
            return std::array<int, 4>{p[0], p[1], p[2], p[3]};
        };
        if (px(42, 50) == px(58, 50)) {
            return "unpinned bars unexpectedly render one color — the "
                   "mosaic check is unprovable";
        }
    }

    // ---- Clip::object_track serialization round trip ----
    {
        Project project;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip tracked;
        tracked.id = "tracked-rt";
        tracked.source_path = "tracked.mp4";   // need not exist
        tracked.track = 0;
        tracked.timeline_start = 0.0;
        tracked.duration = 4.0;
        tracked.object_track.label = "person";
        tracked.object_track.classes = {"car", "person"};
        ReframeSample s0, s1;
        s0.t = 0.5;
        s0.cx = 0.3f;
        s0.cy = 0.6f;
        s0.w = 0.2f;
        s0.h = 0.4f;
        s0.confidence = 0.77f;
        s1.t = 1.0;
        s1.cx = 0.7f;
        s1.cy = 0.4f;
        s1.w = 0.3f;
        s1.h = 0.2f;
        s1.confidence = 0.55f;
        s1.scene_boundary = true;
        tracked.object_track.samples = {s0, s1};
        project.clips.push_back(tracked);

        const std::string path =
            Utf8Path(SmokeTempDir() / "smoke_track.xerproj");
        if (!ProjectStore::Save(project, path, &err)) {
            return "Save failed: " + err;
        }
        Project loaded;
        if (!ProjectStore::Load(path, &loaded, &err)) {
            return "Load failed: " + err;
        }
        const Clip* back = FindClip(loaded, "tracked-rt");
        if (back == nullptr ||
            back->object_track.samples.size() != 2 ||
            back->object_track.label != "person" ||
            back->object_track.classes.size() != 2 ||
            back->object_track.classes[0] != "car") {
            return "object_track did not survive the round trip";
        }
        const ReframeSample& r0 = back->object_track.samples[0];
        const ReframeSample& r1 = back->object_track.samples[1];
        if (r0.t != 0.5 || std::abs(r0.cx - 0.3f) > 1e-6f ||
            std::abs(r0.cy - 0.6f) > 1e-6f ||
            std::abs(r0.w - 0.2f) > 1e-6f ||
            std::abs(r0.h - 0.4f) > 1e-6f ||
            std::abs(r0.confidence - 0.77f) > 1e-6f ||
            r0.scene_boundary || r1.t != 1.0 ||
            std::abs(r1.cx - 0.7f) > 1e-6f || !r1.scene_boundary) {
            return "object_track sample fields did not survive the "
                   "round trip";
        }
    }
    return {};
}

// Silero VAD (ai/silero_vad.h) + the VAD silence-cut upgrade
// (ai/silence_detect.h method "auto"/"vad"). What MUST hold
// regardless of the environment: a missing model path, an unloaded
// session and a bad DetectSpeech call all produce reasons, never
// crashes. When silero_vad.onnx + an ONNX backend are staged the
// check runs real inference on synthetic PCM (silence -> speech-like
// harmonics -> loud noise -> silence): the noise region must read
// non-speech under VAD (where RMS sees it as non-silent — the
// upgrade's whole point), the harmonic region must stay speech, and
// DetectSilences must report method "vad" while "rms" still answers
// the envelope path.
std::string CheckSileroVad() {
    std::string err;

    // ---- graceful-degradation contract (always verified) ----
    Ai::SileroVad vad;
    if (vad.IsLoaded()) return "fresh SileroVad reports loaded";
    if (vad.Load("definitely/missing/silero_vad.onnx", &err)) {
        return "SileroVad::Load accepted a nonexistent model";
    }
    if (err.empty()) {
        return "SileroVad::Load failed a missing model with no "
               "reason";
    }
    std::vector<float> window(Ai::kVadWindowSamples, 0.0f);
    float prob = -1.0f;
    if (vad.ProcessWindow(window.data(), &prob, &err)) {
        return "ProcessWindow ran on an unloaded session";
    }
    err.clear();
    std::vector<Ai::SpeechSpan> spans;
    if (Ai::DetectSpeech(vad, window.data(), window.size(),
                         Ai::VadSpeechParams{}, &spans, nullptr,
                         &err)) {
        return "DetectSpeech ran on an unloaded session";
    }
    err.clear();

    // DetectSilences on a missing file still fails the documented way.
    Ai::SilenceResult res;
    if (Ai::DetectSilences("definitely/missing/audio.wav",
                           Ai::SilenceDetectParams{}, false, &res,
                           &err)) {
        return "DetectSilences accepted a nonexistent file";
    }

    // ---- real-model path: only when staged + loadable ----
    const std::string model = Ai::SileroVadModel();
    if (model.empty() ||
        XER::AI::Onnx::ActiveBackendId().empty()) {
        std::fprintf(stdout,
                     "  silero_vad: SKIP (model or ONNX backend "
                     "unavailable)\n");
        std::fflush(stdout);
        return {};
    }
    if (!vad.Load(model, &err)) {
        std::fprintf(stdout,
                     "  silero_vad: SKIP (staged model rejected: "
                     "%s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }

    // Synthetic 4.5 s @16 kHz mono:
    //   0.0-1.0  digital silence
    //   1.0-2.5  speech-like harmonic stack (150 Hz + partials,
    //            4 Hz AM) over light noise
    //   2.5-3.5  LOUD broadband noise only — RMS reads energy
    //            (not silence), VAD should read non-speech
    //   3.5-4.5  digital silence
    const int n = static_cast<int>(4.5 * Ai::kVadSampleRate);
    std::vector<float> pcm(static_cast<std::size_t>(n), 0.0f);
    std::uint32_t rng = 0x13579BDFu;
    auto noise = [&] {
        rng = rng * 1664525u + 1013904223u;
        return (static_cast<float>((rng >> 9) & 0x7FFFFF) /
                    4194304.0f -
                1.0f);
    };
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / Ai::kVadSampleRate;
        if (t >= 1.0 && t < 2.5) {
            float v = 0.0f;
            for (int k = 1; k <= 8; ++k) {
                v += (0.35f / k) *
                     std::sin(2.0f * 3.14159265f * 150.0f * k *
                              static_cast<float>(t));
            }
            // Shallow 4 Hz AM — a speech-like envelope without troughs
            // deep enough to dip the model under speech_off.
            pcm[static_cast<std::size_t>(i)] =
                v * (0.75f + 0.25f * std::sin(2.0f * 3.14159265f *
                                            4.0f *
                                            static_cast<float>(t))) +
                noise() * 0.02f;
        } else if (t >= 2.5 && t < 3.5) {
            pcm[static_cast<std::size_t>(i)] = noise() * 0.3f;
        }
    }

    // Streaming contract: per-window probs over the file — speech
    // windows inside [1.0,2.5], non-speech elsewhere.
    std::vector<float> probs;
    if (!Ai::DetectSpeech(vad, pcm.data(), pcm.size(),
                          Ai::VadSpeechParams{}, &spans, &probs,
                          &err)) {
        return "DetectSpeech failed on synthetic PCM: " + err;
    }
    const std::size_t expected_windows =
        (pcm.size() + Ai::kVadWindowSamples - 1) /
        Ai::kVadWindowSamples;
    if (probs.size() != expected_windows) {
        return "DetectSpeech produced the wrong window count";
    }
    bool any_prob_out_of_range = false;
    for (const float p : probs) {
        if (!(p >= 0.0f && p <= 1.0f)) any_prob_out_of_range = true;
    }
    if (any_prob_out_of_range) {
        return "VAD probability outside [0,1]";
    }
    std::fprintf(stdout,
                 "  silero_vad: %zu windows -> %zu speech span(s)\n",
                 probs.size(), spans.size());
    for (const Ai::SpeechSpan& s : spans) {
        std::fprintf(stdout,
                     "    speech %.2f-%.2f (mean prob %.2f)\n", s.t0,
                     s.t1, s.mean_prob);
    }
    std::fflush(stdout);
    const auto overlaps = [](const std::vector<Ai::SpeechSpan>& v,
                             double t0, double t1) {
        for (const Ai::SpeechSpan& s : v) {
            if (s.t1 > t0 && s.t0 < t1) return true;
        }
        return false;
    };
    // Speech preserved: most of [1.2, 2.3] must be covered by speech.
    double covered = 0.0;
    for (const Ai::SpeechSpan& s : spans) {
        covered += std::max(0.0, std::min(s.t1, 2.3) -
                                     std::max(s.t0, 1.2));
    }
    if (covered < 0.8) {
        return "VAD dropped the harmonic speech region";
    }
    // Noise reads non-speech: no speech span may cover [2.7, 3.3].
    if (overlaps(spans, 2.7, 3.3)) {
        return "VAD called loud broadband noise speech";
    }
    // Real silence reads non-speech too.
    if (overlaps(spans, 0.1, 0.9) || overlaps(spans, 3.7, 4.4)) {
        return "VAD called digital silence speech";
    }

    // End-to-end silence cut: write the PCM as a PCM16 WAV and run
    // DetectSilences under both methods on the same file.
    const auto dir = SmokeTempDir() / "silero_vad";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto wav_path = dir / "vad_test.wav";
    {
        std::ofstream w(wav_path,
                        std::ios::binary | std::ios::trunc);
        if (!w) return "could not write the VAD test WAV";
        const std::uint32_t data_size =
            static_cast<std::uint32_t>(pcm.size() * 2);
        const std::uint32_t riff_size = 36 + data_size;
        const char riff[] = "RIFF";
        const char wave[] = "WAVE";
        const char fmt[] = "fmt ";
        const char data_id[] = "data";
        const std::uint32_t fmt_len = 16;
        const std::uint16_t audio_fmt = 1;      // PCM
        const std::uint16_t channels = 1;
        const std::uint32_t rate = Ai::kVadSampleRate;
        const std::uint32_t byte_rate = rate * 2;
        const std::uint16_t align = 2;
        const std::uint16_t bits = 16;
        const auto put = [&](const void* p, std::size_t bytes) {
            w.write(static_cast<const char*>(p),
                    static_cast<std::streamsize>(bytes));
        };
        put(riff, 4);
        put(&riff_size, 4);
        put(wave, 4);
        put(fmt, 4);
        put(&fmt_len, 4);
        put(&audio_fmt, 2);
        put(&channels, 2);
        put(&rate, 4);
        put(&byte_rate, 4);
        put(&align, 2);
        put(&bits, 2);
        put(data_id, 4);
        put(&data_size, 4);
        std::vector<std::int16_t> s16(pcm.size());
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            s16[i] = static_cast<std::int16_t>(std::lround(
                std::clamp(pcm[i], -1.0f, 1.0f) * 32767.0f));
        }
        put(s16.data(), s16.size() * 2);
    }
    const std::string wav_utf8 = Utf8Path(wav_path);

    // RMS method on the same file: the loud noise region must NOT be
    // flagged silent (the envelope sees energy) — the VAD upgrade's
    // distinguishing behavior.
    Ai::SilenceDetectParams rms_params;
    rms_params.method = "rms";
    rms_params.min_silence = 0.3;
    Ai::SilenceResult rms_result;
    Ai::SilenceDetectParams vad_params;
    vad_params.method = "auto";
    vad_params.min_silence = 0.3;
    vad_params.vad_min_silence = 0.3;
    try {
        if (!Ai::DetectSilences(wav_utf8, rms_params, false,
                                &rms_result, &err)) {
            return "DetectSilences(rms) failed on the test WAV: " +
                   err;
        }
        // VAD/auto on the same file: silence = "not speech". The
        // noise region must now appear as a silence; the harmonic
        // region must not. Environments where the model failed to
        // load earlier never reach this point.
        if (!Ai::DetectSilences(wav_utf8, vad_params, false, &res,
                                &err)) {
            return "DetectSilences(auto) failed on the test WAV: " +
                   err;
        }
    } catch (const std::exception& e) {
        // A throwing analysis path is a defect worth a FAIL line,
        // not a suite crash (audio_pcm's drain regression was found
        // exactly this way).
        return std::string("DetectSilences threw std::exception: ") +
               e.what();
    } catch (...) {
        return "DetectSilences threw a non-std exception";
    }
    if (rms_result.method != "rms") {
        return "DetectSilences(rms) did not report method rms";
    }
    const auto silent_overlap = [](const Ai::SilenceResult& r,
                                   double t0, double t1) {
        for (const Ai::SilenceSpan& s : r.silences) {
            if (s.t1 > t0 && s.t0 < t1) return true;
        }
        return false;
    };
    if (silent_overlap(rms_result, 2.7, 3.3)) {
        return "RMS flagged loud noise as silence — the fixture is "
               "unprovable";
    }
    if (res.method != "vad") {
        return "DetectSilences(auto) did not take the VAD path "
               "(method='" +
               res.method + "', detail='" + res.method_detail + "')";
    }
    if (!silent_overlap(res, 2.7, 3.3)) {
        return "VAD silence cut missed the noise region";
    }
    if (silent_overlap(res, 1.3, 2.2)) {
        return "VAD silence cut ate the speech region";
    }
    if (!silent_overlap(res, 0.0, 0.9) ||
        !silent_overlap(res, 3.7, 4.5)) {
        return "VAD silence cut missed the real silence";
    }
    return {};
}

// YuNet face detection (ai/face_detect.h) — the auto face-blur scan.
// What MUST hold regardless of the environment: Load on a missing
// file fails with a reason, Detect on an unloaded detector returns
// empty, and ScanClipFaces degrades like ScanClipTrack. Decode+NMS
// are exercised WITHOUT a model via synthetic head tensors (a
// synthetic drawn face would not reliably trigger YuNet — documented
// fixture limit). When the model + backend are staged, Detect runs
// on a synthetic frame end to end (zero faces is a legal result).
std::string CheckFaceDetect() {
    std::string err;

    // ---- graceful-degradation contract (always verified) ----
    Ai::FaceDetector detector;
    if (detector.IsLoaded()) return "fresh FaceDetector reports loaded";
    if (detector.Load("definitely/missing/yunet.onnx", &err)) {
        return "FaceDetector::Load accepted a nonexistent model";
    }
    if (err.empty()) {
        return "FaceDetector::Load failed a missing model with no "
               "reason";
    }
    std::vector<std::uint8_t> bgra(160 * 120 * 4, 0x80);
    for (std::size_t p = 3; p < bgra.size(); p += 4) bgra[p] = 0xFF;
    if (!detector.Detect(bgra.data(), 160, 120, 160 * 4,
                         Ai::FaceDetectParams{})
             .empty()) {
        return "unloaded FaceDetector returned detections";
    }

    Clip clip;
    clip.id = "face-scan";
    clip.source_path = "definitely/missing/media.mp4";
    clip.duration = 5.0;
    ClipTrack track;
    if (Ai::ScanClipFaces(clip, "", Ai::ReframeScanParams{},
                          Ai::FaceDetectParams{}, nullptr, nullptr,
                          &track, &err)) {
        return "ScanClipFaces succeeded with no model";
    }
    if (err.empty() || !track.samples.empty()) {
        return "no-model face scan did not report a reason";
    }
    Clip generated = MakeGeneratedClip("genf", 0, 0.0, 1.0,
                                       SourceKind::ColorBars,
                                       "smpte75");
    if (Ai::ScanClipFaces(generated, "x.onnx",
                          Ai::ReframeScanParams{},
                          Ai::FaceDetectParams{}, nullptr, nullptr,
                          &track, &err)) {
        return "ScanClipFaces scanned a generated clip";
    }

    // ---- decode + NMS math (model-free) ----
    // 640x640 input: stride-8 head has 80x80 = 6400 anchors. Plant
    // two overlapping hits (one stronger) at cells (r=10,c=20) and
    // (r=10,c=21) plus a distant one at (r=60,c=60); NMS must keep
    // the strong + the distant and suppress the weak neighbor.
    {
        constexpr int S = Ai::kYunetInputSize;   // 640
        const std::size_t count =
            static_cast<std::size_t>(S / 8) * (S / 8);
        std::vector<float> cls(count, 0.0f), obj(count, 0.0f);
        std::vector<float> bbox(count * 4, 0.0f);
        std::vector<float> kps(count * 10, 0.0f);
        const int cols = S / 8;
        const auto plant = [&](int r, int c, float score,
                               float dx, float dy, float log_w,
                               float log_h) {
            const std::size_t idx =
                static_cast<std::size_t>(r) * cols + c;
            cls[idx] = score;
            obj[idx] = score;
            bbox[idx * 4 + 0] = dx;
            bbox[idx * 4 + 1] = dy;
            bbox[idx * 4 + 2] = log_w;
            bbox[idx * 4 + 3] = log_h;
        };
        plant(10, 20, 0.9f, 0.25f, -0.25f, std::log(4.0f),
              std::log(4.0f));
        plant(10, 21, 0.5f, 0.0f, 0.0f, std::log(4.0f),
              std::log(4.0f));
        plant(60, 60, 0.8f, 0.0f, 0.0f, std::log(8.0f),
              std::log(8.0f));
        Ai::YunetHeadView head;
        head.stride = 8;
        head.cls = cls.data();
        head.obj = obj.data();
        head.bbox = bbox.data();
        head.kps = kps.data();
        head.count = count;
        Ai::FaceDetectParams p;
        p.score_threshold = 0.3f;
        const auto faces =
            Ai::DecodeYunet(&head, 1, S, S, p);
        if (faces.size() != 2) {
            return "DecodeYunet kept " +
                   std::to_string(faces.size()) +
                   " boxes (expected 2 after NMS)";
        }
        // NMS keeps score order: the 0.9 box first.
        const Ai::FaceBox& f = faces[0];
        if (std::fabs(f.score - 0.9f) > 1e-3f) {
            return "DecodeYunet did not keep the strongest box first";
        }
        // Cell (r=10,c=20): cx=(20.25)*8=162, cy=(9.75)*8=78,
        // w=h=exp(log4)*8=32 -> x=146, y=62.
        if (std::fabs(f.x - 146.0f) > 0.6f ||
            std::fabs(f.y - 62.0f) > 0.6f ||
            std::fabs(f.w - 32.0f) > 0.6f ||
            std::fabs(f.h - 32.0f) > 0.6f) {
            return "DecodeYunet box decode is off the anchor math";
        }
    }

    // ---- registered descriptor + compositor branch ----
    {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:xer.fx.face_blur");
        if (desc == nullptr || !desc->builtin ||
            desc->id != Ai::kFaceBlurId) {
            return "builtin:xer.fx.face_blur is not registered";
        }
        if (desc->params.size() != 3 ||
            std::string(desc->params[0].name) != "block" ||
            std::string(desc->params[1].name) != "padding" ||
            std::string(desc->params[2].name) != "smoothing") {
            return "face_blur param table is not {block, padding, "
                   "smoothing}";
        }
        // The face-blur id must pixelate the same way track_mosaic
        // does: same clip + track as the object_track check, thin
        // effect id swapped.
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip bars_clip = MakeGeneratedClip("bars-f", 0, 0.0, 1.0,
                                           SourceKind::ColorBars,
                                           "smpte75");
        bars_clip.effects.push_back(
            {"builtin:xer.fx.face_blur", {128.0f, 0.0f, 0.0f}});
        ReframeSample s;
        s.t = 0.0;
        s.cx = 0.25f;
        s.cy = 0.5f;
        s.w = 0.10f;
        s.h = 1.0f;
        s.confidence = 0.9f;
        bars_clip.object_track.label = "face";
        bars_clip.object_track.samples.push_back(s);
        project.clips.push_back(bars_clip);
        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> out;
        const bool rendered = compositor.RenderFrameAt(0.0, &out, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const auto px = [&out](int x, int y) {
            const std::uint8_t* p =
                out.data() +
                (static_cast<std::size_t>(y) * 200 + x) * 4;
            return std::array<int, 4>{p[0], p[1], p[2], p[3]};
        };
        if (px(42, 50) != px(58, 50)) {
            return "face_blur did not pixelate the tracked face box";
        }
    }

    // ---- real-model path: only when staged + loadable ----
    const std::string model = Ai::FaceDetectModel();
    if (model.empty() ||
        XER::AI::Onnx::ActiveBackendId().empty()) {
        std::fprintf(stdout,
                     "  face_detect: SKIP (model or ONNX backend "
                     "unavailable)\n");
        std::fflush(stdout);
        return {};
    }
    if (!detector.Load(model, &err)) {
        std::fprintf(stdout,
                     "  face_detect: SKIP (staged model rejected: "
                     "%s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }
    // A uniform gray frame is legal inference input — zero faces is
    // the expected result; the contract is "returns, does not
    // crash" (synthetic faces are fixture-limited per the header).
    const auto faces = detector.Detect(bgra.data(), 160, 120,
                                       160 * 4,
                                       Ai::FaceDetectParams{});
    std::fprintf(stdout,
                 "  face_detect: inference ran on '%s' -> %zu "
                 "detection(s)\n",
                 detector.BackendId().c_str(), faces.size());
    std::fflush(stdout);
    for (const Ai::FaceBox& f : faces) {
        if (!std::isfinite(f.x) || !std::isfinite(f.w) || f.w <= 0.0f ||
            f.h <= 0.0f) {
            return "face_detect produced a degenerate box";
        }
    }
    return {};
}

// SAM2.1-hiera-tiny mask tracking (ai/sam2_session.h +
// ai/sam2_track.h) — the "Track Mask (AI)" clip seam landing
// Clip::mask_track + builtin:xer.fx.track_mask. Always verified:
// the graceful-degradation contract (unloaded sessions reject
// cleanly, scans fail with a reason), every model-free mask helper
// (bounds/centroid/packed-bits/IoU), MaskTrackAt's crossfade +
// scene-boundary snap, the armed-click transform map, the
// registered descriptor, and a synthetic-mask isolate render
// through the compositor branch. The LIVE model path is env-gated
// behind XER_SMOKE_SAM2_LIVE=1: ORT hard-crashes inside SAM-family
// encoder/decoder inference on this machine (ai_lab_ops is
// commented out of kChecks for the same confirmed 2026-11-23 crash
// — a crash kills the whole suite, so real inference is opt-in
// until the ORT/model stack is fixed).
std::string CheckSam2Track() {
    std::string err;

    // ---- graceful-degradation contract (always verified) ----
    Ai::Sam2Encoder encoder;
    if (encoder.IsLoaded()) {
        return "fresh Sam2Encoder reports loaded";
    }
    if (encoder.Load("definitely/missing/vision_encoder.onnx",
                     &err)) {
        return "Sam2Encoder::Load accepted a nonexistent model";
    }
    if (err.empty()) {
        return "Sam2Encoder::Load failed a missing model with no "
               "reason";
    }
    err.clear();
    Ai::Sam2Embedding emb;
    std::vector<std::uint8_t> bgra(160 * 120 * 4, 0x40);
    for (std::size_t p = 3; p < bgra.size(); p += 4) bgra[p] = 0xFF;
    if (encoder.Encode(bgra.data(), 160, 120, 160 * 4, &emb, &err)) {
        return "Sam2Encoder::Encode ran on an unloaded session";
    }
    err.clear();
    Ai::Sam2Decoder decoder;
    if (decoder.IsLoaded()) {
        return "fresh Sam2Decoder reports loaded";
    }
    if (decoder.Load("definitely/missing/decoder.onnx", &err)) {
        return "Sam2Decoder::Load accepted a nonexistent model";
    }
    err.clear();
    Ai::Sam2Prompt prompt;
    prompt.points = {80.0f, 60.0f};
    prompt.labels = {1};
    Ai::Sam2Mask mask;
    if (decoder.Decode(emb, prompt, &mask, &err)) {
        return "Sam2Decoder::Decode ran on an unloaded session";
    }
    err.clear();
    Clip scan_clip;
    scan_clip.id = "sam-scan";
    scan_clip.source_path = "definitely/missing/media.mp4";
    scan_clip.duration = 5.0;
    ClipMaskTrack track;
    if (Ai::ScanClipMaskTrack(scan_clip, "", "",
                              Ai::Sam2TrackParams{}, nullptr, nullptr,
                              &track, &err)) {
        return "ScanClipMaskTrack succeeded with no models";
    }
    if (err.empty() || !track.samples.empty()) {
        return "no-model mask scan did not report a reason";
    }
    err.clear();
    Clip generated = MakeGeneratedClip("gensam", 0, 0.0, 1.0,
                                       SourceKind::ColorBars,
                                       "smpte75");
    if (Ai::ScanClipMaskTrack(generated, "e.onnx", "d.onnx",
                              Ai::Sam2TrackParams{}, nullptr, nullptr,
                              &track, &err)) {
        return "ScanClipMaskTrack scanned a generated clip";
    }
    err.clear();

    // ---- mask math (model-free) ---------------------------------
    // 8x8 logits with a 4x4 >=0 block at rows/cols 2..5 (centered):
    // bounds + centroid land on the block, the packed-bit resample
    // reproduces it, and IoU math agrees with the shifts.
    {
        constexpr int MW = 8, MH = 8;
        std::vector<float> logits(MW * MH, -1.0f);
        for (int y = 2; y <= 5; ++y) {
            for (int x = 2; x <= 5; ++x) {
                logits[y * MW + x] = 1.0f;
            }
        }
        float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
        if (!Ai::Sam2MaskBounds(logits.data(), MW, MH, &cx, &cy, &w,
                                &h)) {
            return "Sam2MaskBounds rejected a non-empty mask";
        }
        // Block covers cols 2..5 -> x span [2,6)/8 = [0.25,0.75) ->
        // center 0.5, width 0.5.
        if (std::fabs(cx - 0.5f) > 1e-3f ||
            std::fabs(cy - 0.5f) > 1e-3f ||
            std::fabs(w - 0.5f) > 1e-3f ||
            std::fabs(h - 0.5f) > 1e-3f) {
            return "Sam2MaskBounds bbox is off the block math";
        }
        // Cell-center mean of cols/rows 2..5 -> (2.5+3.5+4.5+5.5)/4
        // = 4 -> 4/8 = 0.5.
        if (!Ai::Sam2MaskCentroid(logits.data(), MW, MH, &cx, &cy) ||
            std::fabs(cx - 0.5f) > 0.02f ||
            std::fabs(cy - 0.5f) > 0.02f) {
            return "Sam2MaskCentroid missed the block center";
        }
        std::vector<float> empty_logits(MW * MH, -1.0f);
        if (Ai::Sam2MaskBounds(empty_logits.data(), MW, MH, &cx, &cy,
                               &w, &h) ||
            Ai::Sam2MaskCentroid(empty_logits.data(), MW, MH, &cx,
                                 &cy)) {
            return "empty mask reported bounds/centroid";
        }
        const auto bits = Ai::Sam2MaskBits(logits.data(), MW, MH, 8,
                                           8);
        if (bits.size() != 8) {
            return "Sam2MaskBits packed the wrong byte count";
        }
        // Cells 2..5 set => each row byte is 0b00111100 = 0x3C.
        for (std::size_t i = 0; i < bits.size(); ++i) {
            const std::uint8_t want =
                (i >= 2 && i <= 5) ? 0x3C : 0x00;
            if (bits[i] != want) {
                return "Sam2MaskBits cell coverage is wrong";
            }
        }
        if (!Ai::Sam2BitAt(bits.data(), 8, 8, 3, 3) ||
            Ai::Sam2BitAt(bits.data(), 8, 8, 0, 0) ||
            Ai::Sam2BitAt(bits.data(), 8, 8, 7, 7)) {
            return "Sam2BitAt reads the wrong cells";
        }
        if (std::fabs(Ai::Sam2BitsIoU(bits.data(), bits.data(), 8,
                                      8) -
                      1.0f) > 1e-6f) {
            return "Sam2BitsIoU self-overlap is not 1";
        }
        const auto empty_bits =
            Ai::Sam2MaskBits(empty_logits.data(), MW, MH, 8, 8);
        if (Ai::Sam2BitsIoU(bits.data(), empty_bits.data(), 8, 8) !=
            0.0f) {
            return "Sam2BitsIoU vs empty is not 0";
        }
        // Shift the block one cell right -> halves the overlap with
        // the original (3x4 vs 4x4 -> IoU 12/20 = 0.6).
        std::vector<float> shifted(MW * MH, -1.0f);
        for (int y = 2; y <= 5; ++y) {
            for (int x = 3; x <= 6; ++x) {
                shifted[y * MW + x] = 1.0f;
            }
        }
        const auto sbits =
            Ai::Sam2MaskBits(shifted.data(), MW, MH, 8, 8);
        if (std::fabs(Ai::Sam2BitsIoU(bits.data(), sbits.data(), 8,
                                      8) -
                      0.6f) > 1e-3f) {
            return "Sam2BitsIoU on a shifted mask is off";
        }
    }

    // ---- MaskTrackAt: crossfade + snap + span ends --------------
    {
        ClipMaskTrack mt;
        mt.grid_w = 8;
        mt.grid_h = 8;
        mt.label = "sam";
        ClipMaskSample a;
        a.t = 0.0;
        a.confidence = 0.9f;
        // Left half covered: cols 0..3 -> 0xF0 per row.
        a.bits.assign(8, 0xF0);
        ClipMaskSample b;
        b.t = 1.0;
        b.confidence = 0.9f;
        // Right half covered: cols 4..7 -> 0x0F.
        b.bits.assign(8, 0x0F);
        mt.samples = {a, b};
        std::vector<std::uint8_t> a8;
        if (!Ai::MaskTrackAt(mt, 0.0, &a8) ||
            a8.size() != 64 || a8[0] != 255 || a8[7] != 0) {
            return "MaskTrackAt at t0 is not the left-half mask";
        }
        if (!Ai::MaskTrackAt(mt, 1.0, &a8) ||
            a8[0] != 0 || a8[7] != 255) {
            return "MaskTrackAt at t1 is not the right-half mask";
        }
        if (!Ai::MaskTrackAt(mt, 0.5, &a8)) {
            return "MaskTrackAt mid-span failed";
        }
        // Crossfade at 0.5: both halves partial (~128).
        if (a8[0] < 100 || a8[0] > 160 || a8[7] < 100 ||
            a8[7] > 160) {
            return "MaskTrackAt did not crossfade between samples";
        }
        // Past the sampled span coverage goes empty.
        if (Ai::MaskTrackAt(mt, 3.0, &a8)) {
            bool any = false;
            for (const std::uint8_t v : a8) any = any || v != 0;
            if (any) {
                return "MaskTrackAt emitted coverage outside the "
                       "span";
            }
        }
        // scene_boundary on b snaps the lerp: at t=0.9 the mask is
        // still fully the a bitmap.
        mt.samples[1].scene_boundary = true;
        if (!Ai::MaskTrackAt(mt, 0.9, &a8) || a8[0] != 255 ||
            a8[7] != 0) {
            return "MaskTrackAt did not snap across a scene "
                   "boundary";
        }
        // Degenerate tracks resolve as "nothing tracked".
        ClipMaskTrack empty_track;
        if (Ai::MaskTrackAt(empty_track, 0.0, &a8)) {
            return "MaskTrackAt accepted an empty track";
        }
        ClipMaskTrack no_grid = mt;
        no_grid.grid_w = 0;
        if (Ai::MaskTrackAt(no_grid, 0.0, &a8)) {
            return "MaskTrackAt accepted a zero-grid track";
        }
    }

    // ---- armed-click -> source-fraction map ----------------------
    {
        Clip clip;
        clip.duration = 4.0;
        // Default transform = identity: a 320x180 src sits
        // un-scaled top-left on the 1920x1080 canvas.
        float fx = 0.0f, fy = 0.0f;
        if (!Ai::CanvasPointToSourceFraction(clip, 0.0, 1920, 1080,
                                           320, 180, 0.08, 0.08,
                                           &fx, &fy)) {
            return "in-rect canvas click did not map";
        }
        // canvas px (153.6, 86.4); rect center (160,90), dw=320,
        // dh=180 -> fractions (0.48, 0.48).
        if (std::fabs(fx - 0.48f) > 0.01f ||
            std::fabs(fy - 0.48f) > 0.01f) {
            return "canvas->source map produced wrong fractions";
        }
        if (Ai::CanvasPointToSourceFraction(clip, 0.0, 1920, 1080,
                                            320, 180, 0.5, 0.5, &fx,
                                            &fy)) {
            return "out-of-rect canvas click mapped anyway";
        }
        // Offset 0.25,0: rect slides right 480px — a click just
        // inside its left edge maps near x-fraction 0.18.
        clip.transform.offset_x = 0.25f;
        if (!Ai::CanvasPointToSourceFraction(clip, 0.0, 1920, 1080,
                                           320, 180, 0.28, 0.08,
                                           &fx, &fy)) {
            return "offset-rect canvas click did not map";
        }
        if (std::fabs(fx - 0.18f) > 0.01f ||
            std::fabs(fy - 0.48f) > 0.01f) {
            return "offset-rect map produced wrong fractions";
        }
    }

    // ---- registered descriptor + compositor isolate branch ------
    {
        const PluginDesc* desc =
            PluginHost::ForEffectType("builtin:xer.fx.track_mask");
        if (desc == nullptr || !desc->builtin ||
            desc->id != Ai::kTrackMaskId) {
            return "builtin:xer.fx.track_mask is not registered";
        }
        if (desc->params.size() != 3 ||
            std::string(desc->params[0].name) != "mode" ||
            std::string(desc->params[1].name) != "feather" ||
            std::string(desc->params[2].name) != "smoothing") {
            return "track_mask param table is not {mode, feather, "
                   "smoothing}";
        }
        // Isolate mode: a mask covering the LEFT half of the grid
        // alphas out the right half — proven by COLOR, not alpha:
        // the masked red matte sits over a blue matte, so the left
        // half reads red and the right half shows the blue plate
        // through the cutout.
        Project project;
        project.width = 200;
        project.height = 100;
        project.fps_num = 15;
        project.fps_den = 1;
        project.tracks.push_back(Track{TrackKind::Video, "V1"});
        project.tracks.push_back(Track{TrackKind::Video, "V2"});
        project.clips.push_back(MakeGeneratedClip(
            "blue", 0, 0.0, 1.0, SourceKind::ColorMatte, "#3366CC"));
        Clip red = MakeGeneratedClip("red", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte,
                                     "#CC3333");
        red.effects.push_back(
            {"builtin:xer.fx.track_mask", {0.0f, 0.0f, 0.5f}});
        ClipMaskSample ms;
        ms.t = 0.0;
        ms.cx = 0.25f;
        ms.cy = 0.5f;
        ms.w = 0.5f;
        ms.h = 1.0f;
        ms.confidence = 0.9f;
        ms.bits.assign(8, 0xF0);   // 8x8 grid, left half set
        red.mask_track.grid_w = 8;
        red.mask_track.grid_h = 8;
        red.mask_track.label = "sam";
        red.mask_track.samples.push_back(ms);
        project.clips.push_back(red);
        Compositor compositor;
        if (!compositor.Init(project, {}, &err)) {
            return "Compositor::Init failed: " + err;
        }
        std::vector<std::uint8_t> out;
        const bool rendered = compositor.RenderFrameAt(0.0, &out, &err);
        compositor.Shutdown();
        if (!rendered) {
            return "RenderFrameAt failed: " + err;
        }
        const auto px = [&out](int x, int y) {
            const std::uint8_t* p =
                out.data() +
                (static_cast<std::size_t>(y) * 200 + x) * 4;
            return std::array<int, 3>{p[0], p[1], p[2]};   // B,G,R
        };
        // Left: the red matte survived the mask (B low, R high).
        // Right: the unmasked side was cut to alpha 0 -> the blue
        // backdrop shows (B high, R low).
        const auto left = px(50, 50);
        const auto right = px(150, 50);
        if (left[2] < 0xB0 || left[0] > 0x60) {
            return "track_mask isolate cut the masked half";
        }
        if (right[0] < 0xB0 || right[2] > 0x60) {
            return "track_mask isolate kept the unmasked half";
        }
    }

    // ---- live model path: opt-in (ORT crash class — see header) --
    if (std::getenv("XER_SMOKE_SAM2_LIVE") == nullptr ||
        !Ai::Sam2ModelsStaged() ||
        XER::AI::Onnx::ActiveBackendId().empty()) {
        std::fprintf(stdout,
                     "  sam2_track: SKIP (live inference opt-in via "
                     "XER_SMOKE_SAM2_LIVE=1, or models/backend "
                     "unavailable)\n");
        std::fflush(stdout);
        return {};
    }
    if (!encoder.Load(Ai::Sam2EncoderModel(), &err)) {
        std::fprintf(stdout,
                     "  sam2_track: SKIP (encoder rejected: %s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }
    if (!decoder.Load(Ai::Sam2DecoderModel(), &err)) {
        std::fprintf(stdout,
                     "  sam2_track: SKIP (decoder rejected: %s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }
    // Synthetic 640x360 frame: dark plate + bright 120x120 square
    // centered at (200,180). Encode once, decode a point prompt at
    // the square's center — the contract is "a non-empty mask whose
    // bounds contain the prompt".
    {
        constexpr int FW = 640, FH = 360;
        std::vector<std::uint8_t> frame(FW * FH * 4, 0x20);
        for (int y = 120; y < 240; ++y) {
            for (int x = 140; x < 260; ++x) {
                std::uint8_t* p =
                    frame.data() +
                    (static_cast<std::size_t>(y) * FW + x) * 4;
                p[0] = p[1] = p[2] = 0xF0;
            }
        }
        for (std::size_t p = 3; p < frame.size(); p += 4) {
            frame[p] = 0xFF;
        }
        if (!encoder.Encode(frame.data(), FW, FH, FW * 4, &emb,
                            &err)) {
            return "Sam2 encode failed on a synthetic frame: " + err;
        }
        if (!emb.Valid() || emb.src_w != FW || emb.src_h != FH) {
            return "Sam2 embedding is degenerate";
        }
        prompt.points = {200.0f, 180.0f};
        if (!decoder.Decode(emb, prompt, &mask, &err)) {
            return "Sam2 decode failed on a point prompt: " + err;
        }
        if (mask.w <= 0 || mask.h <= 0 || mask.logits.empty()) {
            return "Sam2 mask is degenerate";
        }
        float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
        if (!Ai::Sam2MaskBounds(mask.logits.data(), mask.w, mask.h,
                                &cx, &cy, &w, &h)) {
            return "Sam2 decoded an empty mask for a bright square";
        }
        // The prompt point in fractions: (200/640, 180/360) =
        // (0.3125, 0.5) — must fall inside the mask bbox.
        if (0.3125f < cx - w * 0.5f || 0.3125f > cx + w * 0.5f ||
            0.5f < cy - h * 0.5f || 0.5f > cy + h * 0.5f) {
            return "Sam2 mask bounds do not contain the prompt "
                   "point";
        }
        std::fprintf(stdout,
                     "  sam2_track: inference ran on '%s' -> mask "
                     "bbox (%.2f,%.2f %.2fx%.2f)\n",
                     decoder.BackendId().c_str(), cx, cy, w, h);
        std::fflush(stdout);
    }
    return {};
}

// Real-ESRGAN x4v3 upscale (ai/img2img_runner.h + ai/upscale_x4v3.h)
// — the wave-2 "AI Upscale 4x" bin bounce. What MUST hold
// regardless of the environment: Load on a missing file fails with
// a reason, RunTile/UpscaleFrame on an unloaded session fail
// cleanly, bad runner params are rejected, and cancellation is
// honored. The tile math is exercised WITHOUT a model: the grid
// covers every frame edge exactly once, the weight ramp reaches
// 0 at a shared edge and 1 at its end, and a synthetic 2x nearest
// tile fn over a luma ramp reassembles the ideal upsample
// bit-exactly (any weight-sum bug doubles or darkens seam pixels).
// When realesr-general-x4v3.onnx + a backend are staged the check
// runs real inference: 64x64 -> 256x256 non-degenerate, and a
// 96x80 frame through the tiled path -> 384x320.
std::string CheckUpscaleX4v3() {
    std::string err;

    // ---- graceful-degradation contract (always verified) ----
    Ai::UpscaleX4v3 upscaler;
    if (upscaler.IsLoaded()) {
        return "fresh UpscaleX4v3 reports loaded";
    }
    if (upscaler.Load("definitely/missing/x4v3.onnx", "", &err)) {
        return "UpscaleX4v3::Load accepted a nonexistent model";
    }
    if (err.empty()) {
        return "UpscaleX4v3::Load failed a missing model with no "
               "reason";
    }
    std::vector<std::uint8_t> out;
    if (upscaler.RunTile(nullptr, 64, 64, 64 * 4, &out, &err)) {
        return "RunTile ran on an unloaded session";
    }
    err.clear();
    int ow = 0, oh = 0;
    if (upscaler.UpscaleFrame(nullptr, 64, 64, 64 * 4,
                              Ai::Img2ImgParams{}, nullptr, nullptr,
                              &out, &ow, &oh, &err)) {
        return "UpscaleFrame ran on an unloaded session";
    }
    err.clear();

    // ---- tile grid + blend math (model-free) --------------------
    // Bad params are rejected outright.
    if (!Ai::ComputeImg2ImgTiles(64, 64, Ai::Img2ImgParams{0, 0, 0})
             .empty() ||
        !Ai::ComputeImg2ImgTiles(
            64, 64, Ai::Img2ImgParams{64, 40, 1}).empty()) {
        return "tile grid accepted degenerate params";
    }
    {
        // 300x200, tile 128, overlap 16 -> x starts {0,112,172}
        // (last shifted to end at 300), y starts {0,72} -> 6 tiles
        // covering the frame exactly.
        Ai::Img2ImgParams gp;
        gp.tile = 128;
        gp.overlap = 16;
        gp.scale = 2;
        const auto tiles = Ai::ComputeImg2ImgTiles(300, 200, gp);
        if (tiles.size() != 6) {
            return "tile grid produced " +
                   std::to_string(tiles.size()) +
                   " tiles (expected 6)";
        }
        for (const Ai::Img2ImgTile& t : tiles) {
            if (t.w != 128 || t.h != 128) {
                return "tile grid produced a non-128 tile";
            }
        }
        std::vector<int> cover(300 * 200, 0);
        for (const Ai::Img2ImgTile& t : tiles) {
            for (int y = t.y; y < t.y + t.h; ++y) {
                for (int x = t.x; x < t.x + t.w; ++x) {
                    ++cover[y * 300 + x];
                }
            }
        }
        for (const int c : cover) {
            if (c < 1) return "tile grid leaves a frame px uncovered";
        }
    }
    // Weight ramp: at a shared edge -> 0, at its far side -> 1.
    {
        const float at_edge =
            Ai::Img2ImgTileWeight(0, 0, 0, 0, 512, 512, 64, 64, 0, 0);
        const float mid_fade =
            Ai::Img2ImgTileWeight(32, 0, 0, 0, 512, 512, 64, 0, 0, 0);
        const float in_clear =
            Ai::Img2ImgTileWeight(64, 0, 0, 0, 512, 512, 64, 0, 0, 0);
        if (at_edge != 0.0f || mid_fade <= 0.0f ||
            mid_fade >= 1.0f || in_clear != 1.0f) {
            return "tile weight ramp is off the smoothstep contract";
        }
    }
    // Blend reassembly: synthetic 2x nearest upscale of a luma ramp
    // must equal the ideal nearest upsample at EVERY output px —
    // shared-zone contributions are position-identical, so any
    // weight-sum error shows as a seam brightness step.
    {
        constexpr int W = 200, H = 150, S = 2;
        std::vector<std::uint8_t> frame(W * H * 4, 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const std::uint8_t v = static_cast<std::uint8_t>(
                    x * 255 / (W - 1));
                std::uint8_t* px = frame.data() + (y * W + x) * 4;
                px[0] = px[1] = px[2] = v;
                px[3] = 0xFF;
            }
        }
        Ai::Img2ImgParams p;
        p.tile = 64;
        p.overlap = 8;
        p.scale = S;
        Ai::Img2ImgTileFn nearest =
            [S](const std::uint8_t* tile, int tw, int th, int ts,
                std::uint8_t* dst, int dw, int dh, int ds,
                std::string*) -> bool {
            if (dw != tw * S || dh != th * S) return false;
            for (int y = 0; y < dh; ++y) {
                const std::uint8_t* srow =
                    tile + (y / S) * ts;
                std::uint8_t* drow = dst + y * ds;
                for (int x = 0; x < dw; ++x) {
                    std::memcpy(drow + x * 4, srow + (x / S) * 4, 4);
                }
            }
            return true;
        };
        std::vector<std::uint8_t> up;
        std::vector<float> prog_pts;
        if (!Ai::RunImageToImage(frame.data(), W, H, W * 4, p,
                                 nearest,
                                 [&prog_pts](float f) {
                                     prog_pts.push_back(f);
                                 },
                                 nullptr, &up, &ow, &oh, &err)) {
            return "tiled run failed on a synthetic frame: " + err;
        }
        if (ow != W * S || oh != H * S ||
            up.size() != static_cast<std::size_t>(W * S * H * S * 4)) {
            return "tiled run produced wrong output dims";
        }
        if (prog_pts.empty() ||
            std::fabs(prog_pts.back() - 1.0f) > 1e-6f) {
            return "tiled run progress never reached 1.0";
        }
        for (int y = 0; y < H * S; ++y) {
            for (int x = 0; x < W * S; ++x) {
                const int sx = x / S;
                const std::uint8_t ideal = static_cast<std::uint8_t>(
                    sx * 255 / (W - 1));
                const std::uint8_t got =
                    up[(static_cast<std::size_t>(y) * W * S + x) * 4];
                if (std::abs(static_cast<int>(got) -
                             static_cast<int>(ideal)) > 1) {
                    return "seam blend deviated from the ideal "
                           "upsample (weight-sum bug)";
                }
            }
        }
        // Cancellation is honored between tiles.
        if (Ai::RunImageToImage(
                frame.data(), W, H, W * 4, p, nearest, nullptr,
                [] { return true; }, &up, &ow, &oh, &err)) {
            return "cancelled tiled run reported success";
        }
        err.clear();
    }

    // ---- real-model path: only when staged + loadable -----------
    const std::string model = Ai::UpscaleModel();
    if (model.empty() ||
        XER::AI::Onnx::ActiveBackendId().empty()) {
        std::fprintf(stdout,
                     "  upscale_x4v3: SKIP (model or ONNX backend "
                     "unavailable)\n");
        std::fflush(stdout);
        return {};
    }
    if (!upscaler.Load(model, "", &err)) {
        std::fprintf(stdout,
                     "  upscale_x4v3: SKIP (staged model rejected: "
                     "%s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }
    {
        // 64x64 diagonal luma ramp -> 256x256; SR models keep mean
        // level and add detail, so a degenerate (flat/NaN) result
        // is the failure being watched for.
        constexpr int FW = 64, FH = 64;
        std::vector<std::uint8_t> frame(FW * FH * 4, 0);
        for (int y = 0; y < FH; ++y) {
            for (int x = 0; x < FW; ++x) {
                const std::uint8_t v = static_cast<std::uint8_t>(
                    std::clamp((x + y) * 2, 0, 255));
                std::uint8_t* px =
                    frame.data() + (y * FW + x) * 4;
                px[0] = px[1] = px[2] = v;
                px[3] = 0xFF;
            }
        }
        std::vector<std::uint8_t> up;
        if (!upscaler.RunTile(frame.data(), FW, FH, FW * 4, &up,
                              &err)) {
            return "x4v3 inference failed on a 64x64 frame: " + err;
        }
        if (up.size() !=
            static_cast<std::size_t>(FW * 4 * FH * 4 * 4)) {
            return "x4v3 output is not 4x the input";
        }
        double mean = 0.0, var = 0.0;
        int nonalpha = 0;
        for (std::size_t p = 0; p < up.size(); p += 4) {
            if (up[p + 3] != 0xFF) nonalpha = 1;
            mean += up[p];
        }
        mean /= static_cast<double>(up.size() / 4);
        for (std::size_t p = 0; p < up.size(); p += 4) {
            var += (up[p] - mean) * (up[p] - mean);
        }
        var /= static_cast<double>(up.size() / 4);
        if (nonalpha || !(var > 1.0) || !std::isfinite(mean)) {
            return "x4v3 produced a degenerate 4x frame";
        }
        // The tiled runner path end-to-end: 96x80 -> 384x320 with
        // 64px tiles + 8px overlap.
        Ai::Img2ImgParams p;
        p.tile = 64;
        p.overlap = 8;
        up.clear();
        if (!upscaler.UpscaleFrame(frame.data(), 64, 64, 64 * 4, p,
                                   nullptr, nullptr, &up, &ow, &oh,
                                   &err)) {
            return "tiled x4v3 upscale failed: " + err;
        }
        if (ow != 256 || oh != 256) {
            return "tiled x4v3 produced wrong output dims";
        }
        std::fprintf(stdout,
                     "  upscale_x4v3: inference ran on '%s' -> "
                     "%dx%d\n",
                     upscaler.BackendId().c_str(), ow, oh);
        std::fflush(stdout);
    }
    return {};
}

// RIFE 4.25-lite frame interpolation (ai/rife_interp.h +
// ai/ai_bounce.h) — the wave-2 "Smooth slow-mo (AI)" clip bounce.
// Always verified: degradation contract, the alignment pad +
// interpolation-plan math, and the bounce transform's index
// mapping (synthetic frames through MakePairInterpTransform). When
// rife_v425lite.onnx is staged a moving-square pair checks the
// middle frame interpolates the square to the halfway position —
// that IS the cv::dnn GridSample verification the research doc
// gates on (a backend that loads but warps wrong fails the
// centroid assert; cv::dnn specifically SKIPs+reports it as the
// documented gate instead of erroring).
std::string CheckRifeInterp() {
    std::string err;

    // ---- graceful-degradation contract (always verified) ----
    Ai::RifeInterp rife;
    if (rife.IsLoaded()) return "fresh RifeInterp reports loaded";
    if (rife.Load("definitely/missing/rife.onnx", "", &err)) {
        return "RifeInterp::Load accepted a nonexistent model";
    }
    if (err.empty()) {
        return "RifeInterp::Load failed a missing model with no "
               "reason";
    }
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> frame(64 * 64 * 4, 0x40);
    for (std::size_t p = 3; p < frame.size(); p += 4) {
        frame[p] = 0xFF;
    }
    if (rife.Interpolate(frame.data(), frame.data(), 64, 64, 64 * 4,
                         0.5f, &out, &err)) {
        return "Interpolate ran on an unloaded session";
    }
    err.clear();

    // ---- pad + plan math (model-free) ---------------------------
    if (Ai::RifePadDim(128) != 128 || Ai::RifePadDim(100) != 128 ||
        Ai::RifePadDim(65) != 128 || Ai::RifePadDim(1) != 64 ||
        Ai::RifePadDim(200) != 256) {
        return "RifePadDim alignment math is wrong";
    }
    {
        const auto p2 = Ai::RifeInterpPlan(2);
        const auto p4 = Ai::RifeInterpPlan(4);
        const auto p3 = Ai::RifeInterpPlan(3);
        if (p2.size() != 1 || std::fabs(p2[0] - 0.5f) > 1e-6f ||
            p4.size() != 3 || std::fabs(p4[0] - 0.25f) > 1e-6f ||
            std::fabs(p4[2] - 0.75f) > 1e-6f || p3.size() != 2 ||
            std::fabs(p3[0] - 1.0f / 3.0f) > 1e-6f ||
            !Ai::RifeInterpPlan(1).empty()) {
            return "RifeInterpPlan produced a wrong timestep set";
        }
    }

    // ---- bounce transforms (model-free) --------------------------
    {
        // MakePairInterpTransform index mapping: 4 synthetic source
        // frames (tagged by a marker byte), factor 2 -> 7 outputs in
        // [f0, i01, f1, i12, f2, i23, f3] order. The fake ctx feeds
        // source pulls from a counter — the transform must pull
        // exactly the frames each pair needs, in order.
        const auto make_src = [](int tag) {
            Ai::AiBounceFrame f;
            f.bgra.assign(4 * 4 * 4, 0);
            std::memset(f.bgra.data(), tag, 4 * 4);
            for (std::size_t p = 3; p < f.bgra.size(); p += 4) {
                f.bgra[p] = 0xFF;
            }
            f.w = 4;
            f.h = 4;
            f.t = tag;
            return f;
        };
        Ai::AiBounceContext ctx;
        int pull_count = 0;
        ctx.next_source = [&](Ai::AiBounceFrame* f,
                              std::string*) -> bool {
            if (pull_count >= 4) return false;
            *f = make_src(10 + pull_count);
            ++pull_count;
            return true;
        };
        std::vector<float> seen_t;
        auto interp =
            [&seen_t](const Ai::AiBounceFrame& a,
                      const Ai::AiBounceFrame& b, float t,
                      std::vector<std::uint8_t>* o,
                      std::string*) -> bool {
            seen_t.push_back(t);
            o->assign(4 * 4 * 4, 0);
            (*o)[0] = static_cast<std::uint8_t>(
                (a.bgra[0] + b.bgra[0]) / 2);
            for (std::size_t p = 3; p < o->size(); p += 4) {
                (*o)[p] = 0xFF;
            }
            return true;
        };
        auto xform = Ai::MakePairInterpTransform(2, interp);
        std::vector<int> tags;
        for (std::size_t i = 0; i < 7; ++i) {
            std::vector<std::uint8_t> o;
            int w = 0, h = 0;
            if (!xform(i, ctx, &o, &w, &h, &err)) {
                return "pair transform failed at output " +
                       std::to_string(i) + ": " + err;
            }
            if (o.empty()) return "pair transform gave an empty frame";
            tags.push_back(o[0]);
        }
        const std::vector<int> expected = {10, 10, 11, 11, 12, 12, 13};
        // interp outputs carry the mean of the pair's marker tags:
        // i01 -> (10+11)/2 = 10 (integer), i12 -> 11, i23 -> 12.
        if (tags != expected) {
            return "pair transform produced a wrong frame order";
        }
        if (seen_t != std::vector<float>{0.5f, 0.5f, 0.5f}) {
            return "pair transform fed wrong timesteps";
        }
        // MakeFrameTransform: one pull per output index.
        pull_count = 0;
        auto one2one = Ai::MakeFrameTransform(
            [](const Ai::AiBounceFrame& s,
               std::vector<std::uint8_t>* o, int* w, int* h,
               std::string*) -> bool {
                if (w != nullptr) *w = s.w * 4;
                if (h != nullptr) *h = s.h * 4;
                o->assign(static_cast<std::size_t>(s.w * 4) *
                              (s.h * 4) * 4,
                          0x80);
                return true;
            });
        for (std::size_t i = 0; i < 3; ++i) {
            std::vector<std::uint8_t> o;
            int w = 0, h = 0;
            if (!one2one(i, ctx, &o, &w, &h, &err)) {
                return "frame transform failed: " + err;
            }
            if (w != 16 || h != 16 ||
                o.size() != static_cast<std::size_t>(16 * 16 * 4)) {
                return "frame transform produced wrong dims";
            }
        }
        if (pull_count != 3) {
            return "frame transform did not pull one source per "
                   "output";
        }
        // Output naming: <stem>.xer.<tag>.mp4 with uniquify.
        const auto dir = SmokeTempDir() / "ai_bounce";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto src_path =
            dir / "clip name.mp4";
        {
            std::ofstream create(src_path);
        }
        const auto first_out = dir / "clip name.xer.upscaled.mp4";
        {
            std::ofstream create(first_out);
        }
        const std::string named = Ai::AiBounceOutputPath(
            Utf8Path(src_path), "upscaled");
        if (named.find("clip name.xer.upscaled.2.mp4") ==
            std::string::npos) {
            return "AiBounceOutputPath did not uniquify: " + named;
        }
    }

    // ---- real-model path: only when staged + loadable -----------
    const std::string model = Ai::RifeModel();
    if (model.empty() ||
        XER::AI::Onnx::ActiveBackendId().empty()) {
        std::fprintf(stdout,
                     "  rife_interp: SKIP (model or ONNX backend "
                     "unavailable)\n");
        std::fflush(stdout);
        return {};
    }
    if (!rife.Load(model, "", &err)) {
        std::fprintf(stdout,
                     "  rife_interp: SKIP (staged model rejected: "
                     "%s)\n",
                     err.c_str());
        std::fflush(stdout);
        return {};
    }
    {
        // Sanity first (catches a broken pipeline regardless of
        // motion): interpolating an identical pair must return the
        // pair — no warp can explain losing a whole frame's content.
        constexpr int FW = 128, FH = 128;
        auto draw = [](int x0) {
            std::vector<std::uint8_t> f(FW * FH * 4, 0);
            for (int y = 48; y < 80; ++y) {
                for (int x = x0; x < x0 + 32; ++x) {
                    std::uint8_t* px = f.data() + (y * FW + x) * 4;
                    px[0] = px[1] = px[2] = 0xFF;
                    px[3] = 0xFF;
                }
            }
            return f;
        };
        const auto a = draw(16);
        const auto b = draw(32);
        {
            std::vector<std::uint8_t> same;
            if (!rife.Interpolate(a.data(), a.data(), FW, FH,
                                  FW * 4, 0.5f, &same, &err)) {
                return "rife failed the identical-pair sanity: " +
                       err;
            }
            double mad = 0.0;
            for (std::size_t p = 0; p < a.size(); p += 4) {
                mad += std::abs(static_cast<int>(same[p]) -
                                static_cast<int>(a[p]));
            }
            mad /= static_cast<double>(a.size() / 4);
            if (mad > 32.0) {
                return "rife mangled an identical-pair midpoint "
                       "(pipeline broken, not a motion problem)";
            }
        }
        // Moving square: 32x32 white block steps +16 px in x —
        // well inside RIFE's real-motion range. t=0.5 must put the
        // bright centroid halfway (center 32 -> 48, midpoint ~40).
        std::vector<std::uint8_t> mid;
        const bool ran =
            rife.Interpolate(a.data(), b.data(), FW, FH, FW * 4,
                             0.5f, &mid, &err);
        const bool cv_dnn =
            rife.BackendId() == "opencv_dnn";
        if (!ran) {
            if (cv_dnn) {
                std::fprintf(stdout,
                             "  rife_interp: SKIP (cv::dnn rejected "
                             "the run — the documented GridSample "
                             "gate: %s)\n",
                             err.c_str());
                std::fflush(stdout);
                return {};
            }
            return "rife inference failed on the moving square: " +
                   err;
        }
        if (mid.size() != static_cast<std::size_t>(FW * FH * 4)) {
            return "rife output is not the input size";
        }
        double cx = 0.0, cy = 0.0, count = 0.0;
        for (int y = 0; y < FH; ++y) {
            for (int x = 0; x < FW; ++x) {
                const std::uint8_t* px =
                    mid.data() + (y * FW + x) * 4;
                if (px[0] > 128 || px[1] > 128 || px[2] > 128) {
                    cx += x;
                    cy += y;
                    count += 1.0;
                }
            }
        }
        if (count < 64.0) {
            if (cv_dnn) {
                std::fprintf(stdout,
                             "  rife_interp: SKIP (cv::dnn produced "
                             "no interpolated object — GridSample "
                             "gate confirmed)\n");
                std::fflush(stdout);
                return {};
            }
            return "rife dropped the moving square entirely";
        }
        cx /= count;
        cy /= count;
        std::fprintf(stdout,
                     "  rife_interp: '%s' midpoint centroid (%.1f, "
                     "%.1f) over %.0f px\n",
                     rife.BackendId().c_str(), cx, cy, count);
        std::fflush(stdout);
        // The square's center moves 32 -> 48; halfway = ~40.
        if (cx < 30.0 || cx > 50.0 || cy < 52.0 || cy > 76.0) {
            if (cv_dnn) {
                std::fprintf(stdout,
                             "  rife_interp: SKIP (cv::dnn warped "
                             "the centroid to %.1f,%.1f — "
                             "GridSample gate confirmed)\n",
                             cx, cy);
                std::fflush(stdout);
                return {};
            }
            return "rife midpoint did not interpolate the square "
                   "halfway";
        }
        // Endpoint contract: t=0/1 return the verbatim inputs.
        std::vector<std::uint8_t> ends;
        if (!rife.Interpolate(a.data(), b.data(), FW, FH, FW * 4,
                              0.0f, &ends, &err) ||
            ends != a) {
            return "rife t=0 did not return frame A verbatim";
        }
    }
    return {};
}
// onnx_session): BiRefNet alpha matting + EfficientSAM smart-select.
// What MUST hold regardless of the environment: bad inputs and a
// missing model produce reasons, never crashes. When the staged
// models are installed (<exe>/assets/models/) the checks go further —
// a synthetic bright-blob-on-dark frame must matte its blob brighter
// than its background, and a click on the blob must produce a mask
// that covers it. Both inferences run on CPU and take seconds-class
// at 1024x1024, well inside the watchdog.
std::string CheckAiLabOps() {
    // ---- graceful-degradation contract (always verified) ----
    std::string err;
    video::Frame frame;
    if (!Lab::AlphaMatte(frame, "whatever.onnx", {}, &err).empty()) {
        return "AlphaMatte accepted an invalid frame";
    }
    if (err.empty()) {
        return "AlphaMatte failed an invalid frame without a reason";
    }
    err.clear();
    video::Frame good;
    if (!good.Allocate(160, 120, video::PixelFormat::BGRA8)) {
        return "frame allocation failed";
    }
    // Synthetic: dark background, bright fg blob — the strongest
    // possible saliency cue for both models.
    for (int y = 0; y < good.Height(); ++y) {
        for (int x = 0; x < good.Width(); ++x) {
            const bool fg = x >= 48 && x < 112 && y >= 32 && y < 88;
            std::uint8_t* p = good.Data() +
                              (static_cast<std::size_t>(y) *
                                   good.Width() +
                               x) *
                                  4;
            p[0] = p[1] = p[2] = fg ? 235 : 24;
            p[3] = 255;
        }
    }
    err.clear();
    if (!Lab::SmartSelectMask(good, {}, "whatever.onnx", {}, &err)
             .empty()) {
        return "SmartSelectMask accepted zero points";
    }
    if (err.empty()) {
        return "SmartSelectMask failed zero points without a reason";
    }
    err.clear();
    if (!Lab::AlphaMatte(good, "definitely/missing.onnx", {}, &err)
             .empty()) {
        return "AlphaMatte accepted a missing model file";
    }
    if (err.empty()) {
        return "AlphaMatte failed a missing model without a reason";
    }

    // ---- mask -> polygon (monitor overlay edge + lasso input) ----
    // Synthetic 20x20 rect mask: the traced contour should recover
    // its four corners after simplification and round-trip through
    // PolygonMask with the same inside/outside classification. Pure
    // CPU — verified unconditionally.
    {
        constexpr int kMW = 64, kMH = 48;
        std::vector<std::uint8_t> m(
            static_cast<std::size_t>(kMW) * kMH, 0);
        for (int y = 10; y < 30; ++y) {
            for (int x = 20; x < 40; ++x) {
                m[static_cast<std::size_t>(y) * kMW + x] = 255;
            }
        }
        const auto poly = Lab::MaskToPolygon(m, kMW, kMH, 1.5f);
        if (poly.size() < 4 || poly.size() > 8) {
            return "MaskToPolygon rect contour has " +
                   std::to_string(poly.size()) + " vertices";
        }
        for (const Lab::IntPoint& p : poly) {
            if (p.x < 19 || p.x > 41 || p.y < 9 || p.y > 31) {
                return "MaskToPolygon vertex outside the rect bounds";
            }
        }
        const auto remask = Lab::PolygonMask(kMW, kMH, poly, 0);
        if (remask.size() != m.size()) {
            return "PolygonMask round trip has wrong size";
        }
        const auto r_at = [&](int x, int y) {
            return remask[static_cast<std::size_t>(y) * kMW + x];
        };
        if (r_at(30, 20) < 200 || r_at(5, 5) != 0) {
            return "mask->polygon->mask round trip lost coverage";
        }
        // Degenerate inputs stay graceful.
        if (!Lab::MaskToPolygon({}, kMW, kMH, 1.0f).empty() ||
            !Lab::MaskToPolygon(m, 0, kMH, 1.0f).empty() ||
            !Lab::MaskToPolygon(m, kMW, kMH + 8, 1.0f).empty()) {
            return "MaskToPolygon accepted a degenerate input";
        }
    }

    // ---- real-model path: only when the staged models are found ----
    const std::string matte_model =
        Lab::FindAiModel("birefnet_lite.onnx");
    const std::string sam_model =
        Lab::FindAiModel("efficientsam_ti.onnx");
    std::fprintf(stdout, "  ai_lab_ops models: birefnet=%s sam=%s\n",
                 matte_model.empty() ? "<none>" : "staged",
                 sam_model.empty() ? "<none>" : "staged");
    std::fflush(stdout);
    if (XER::AI::Onnx::ActiveBackendId().empty()) {
        return {};   // no inference runtime — graceful skip
    }
    if (!matte_model.empty()) {
        err.clear();
        const auto matte =
            Lab::AlphaMatte(good, matte_model, {}, &err);
        if (matte.empty()) {
            return "BiRefNet matting failed: " + err;
        }
        if (matte.size() != 160u * 120u) {
            return "matte has wrong size";
        }
        // Expected extremes: the fg blob must read clearly brighter
        // than the background corner.
        const auto at = [&](int x, int y) {
            return matte[static_cast<std::size_t>(y) * 160 + x];
        };
        double fg = 0.0, bg = 0.0;
        for (int y = 50; y < 70; ++y)
            for (int x = 70; x < 90; ++x) fg += at(x, y);
        for (int y = 2; y < 12; ++y)
            for (int x = 2; x < 12; ++x) bg += at(x, y);
        fg /= 400.0;
        bg /= 100.0;
        std::fprintf(stdout, "  ai_lab_ops matte: fg=%.1f bg=%.1f\n",
                     fg, bg);
        std::fflush(stdout);
        if (fg <= bg + 20.0) {
            return "matte did not separate fg blob from bg";
        }
        // RemoveBackground applies the matte as the frame alpha.
        err.clear();
        const video::Frame cut =
            Lab::RemoveBackground(good, matte_model, {}, &err);
        if (!cut.IsValid()) {
            return "RemoveBackground failed: " + err;
        }
        const std::uint8_t* ca =
            cut.Data() + (70 * cut.Width() + 80) * 4;
        const std::uint8_t* cb = cut.Data() + (5 * cut.Width() + 5) * 4;
        if (ca[3] <= cb[3]) {
            return "cutout alpha not brighter on fg than bg";
        }
    }
    if (!sam_model.empty()) {
        err.clear();
        const auto mask = Lab::SmartSelectMask(
            good, {Lab::IntPoint{80, 60}}, sam_model, {}, &err);
        if (mask.empty()) {
            return "EfficientSAM select failed: " + err;
        }
        if (mask.size() != 160u * 120u) {
            return "smart-select mask has wrong size";
        }
        const auto at = [&](int x, int y) {
            return mask[static_cast<std::size_t>(y) * 160 + x];
        };
        int covered = 0;
        for (int y = 45; y < 75; y += 5) {
            for (int x = 65; x < 95; x += 5) {
                if (at(x, y) > 0) ++covered;
            }
        }
        if (at(80, 60) == 0 || covered < 20) {
            return "smart-select mask does not cover the clicked blob";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// photo_m1 — the critical M1 correctness property: stroke -> undo ->
// redo must be pixel-exact through the tile-memento path, and tile
// copy-on-write must isolate clones. Headless; the only file touched
// is a throwaway PNG in SmokeTempDir.
// ---------------------------------------------------------------------------
std::string CheckPhotoM1() {
    namespace ph = Photo;

    // 1) Tile copy-on-write: clones share tiles, writes isolate.
    {
        ph::TiledDevice dev(200, 140);
        dev.WritePixel(10, 10, ph::Pixel::RGBA(255, 0, 0));
        auto snap = dev.Clone();
        dev.WritePixel(20, 10, ph::Pixel::RGBA(0, 255, 0));
        if (snap->ReadPixel(20, 10) != ph::Pixel::Transparent()) {
            return "COW: write to source leaked into clone";
        }
        snap->WritePixel(10, 10, ph::Pixel::RGBA(0, 0, 255));
        if (dev.ReadPixel(10, 10) != ph::Pixel::RGBA(255, 0, 0)) {
            return "COW: write to clone leaked into source";
        }
    }

    // 2) Transaction memento -> rollback / re-apply pixel-exact.
    {
        ph::TiledDevice dev(200, 140);
        dev.Fill({0, 0, 20, 20}, ph::Pixel::RGBA(200, 200, 200));
        const auto before = dev.ReadRegion(dev.Bounds());
        dev.BeginTransaction();
        dev.Fill({0, 0, 64, 64}, ph::Pixel::RGBA(9, 9, 9));
        auto m = dev.EndTransaction();
        const auto after_fill = dev.ReadRegion(dev.Bounds());
        if (before == after_fill) {
            return "fill produced no change inside transaction";
        }
        auto inv = dev.ApplyMemento(*m);
        if (dev.ReadRegion(dev.Bounds()) != before) {
            return "memento undo not pixel-exact";
        }
        dev.ApplyMemento(*inv);
        if (dev.ReadRegion(dev.Bounds()) != after_fill) {
            return "memento redo not pixel-exact";
        }
    }

    // 3) Document: fill -> brush stroke -> undo -> redo, all
    //    pixel-exact via PhotoHistory + tile mementos.
    ph::Document doc(192, 128);
    ph::PhotoHistory hist(doc);
    ph::LayerNode& layer = doc.AddRasterLayer("paint");
    ph::TiledDevice* dev = doc.LayerDevice(layer.Id());
    if (dev == nullptr) return "raster layer has no device";

    if (!ph::FillLayer(doc, layer.Id(), ph::Pixel::RGBA(250, 250, 250),
                       &hist)) {
        return "FillLayer failed";
    }
    const auto pre_stroke = dev->ReadRegion(dev->Bounds());

    ph::BrushParams params;
    params.diameter_px = 18.0f;
    params.hardness = 0.9f;
    params.spacing = 0.3f;
    params.flow = 0.8f;
    params.jitter_pos = 0.15f;   // jitter on — must still round-trip
    params.rng_seed = 7;         // deterministic
    params.color = ph::Pixel::RGBA(30, 80, 220, 255);
    ph::BrushEngine brush(params);
    const std::vector<ph::StrokePoint> pts = {
        {30.0f, 40.0f, 1.0f}, {60.0f, 60.0f, 0.9f},
        {90.0f, 45.0f, 0.7f}, {120.0f, 70.0f, 1.0f},
    };
    ph::IntRect dirty;
    if (!brush.PaintStrokeOnLayer(doc, layer.Id(), pts, &hist, &dirty)) {
        return "PaintStrokeOnLayer failed";
    }
    if (dirty.IsEmpty()) return "stroke reported empty dirty rect";
    const auto post_stroke = dev->ReadRegion(dev->Bounds());
    if (pre_stroke == post_stroke) return "stroke changed no pixels";

    if (!hist.Undo()) return "undo of stroke failed";
    if (dev->ReadRegion(dev->Bounds()) != pre_stroke) {
        return "stroke undo not pixel-exact";
    }
    if (!hist.Redo()) return "redo of stroke failed";
    if (dev->ReadRegion(dev->Bounds()) != post_stroke) {
        return "stroke redo not pixel-exact";
    }
    // Undo the fill too (it was recorded before the stroke).
    if (!hist.Undo() || !hist.Undo()) return "undo chain broken";
    if (dev->ReadRegion(dev->Bounds()) !=
        std::vector<uint8_t>(
            static_cast<size_t>(dev->Bounds().w) * dev->Bounds().h * 4,
            0)) {
        return "fill undo did not restore transparent layer";
    }
    if (!hist.Redo() || !hist.Redo()) return "redo chain broken";
    if (dev->ReadRegion(dev->Bounds()) != post_stroke) {
        return "fill+stroke redo not pixel-exact";
    }

    // 4) Eraser variant — alpha must drop where the stroke passes.
    {
        ph::BrushParams er;
        er.eraser = true;
        er.diameter_px = 24.0f;
        er.hardness = 0.95f;
        er.rng_seed = 3;
        ph::BrushEngine eraser(er);
        const std::vector<ph::StrokePoint> epts = {
            {10.0f, 10.0f, 1.0f}, {50.0f, 10.0f, 1.0f},
        };
        if (!eraser.PaintStrokeOnLayer(doc, layer.Id(), epts, &hist,
                                       nullptr)) {
            return "eraser stroke failed";
        }
        if (dev->ReadPixel(30, 10).a >= 250) {
            return "eraser did not reduce alpha";
        }
    }

    // 5) PNG round trip through src/image loader/saver.
    {
        const std::string path =
            Utf8Path(SmokeTempDir() / "photo_m1.png");
        std::string err;
        if (!doc.SavePng(path, &err)) {
            return "SavePng failed: " + err;
        }
        auto back = ph::Document::OpenPng(path, &err);
        if (!back) return "OpenPng failed: " + err;
        if (back->CanvasWidth() != 192 || back->CanvasHeight() != 128) {
            return "PNG round trip changed canvas size";
        }
    }

    // 6) Gradient fill sanity + flatten produces a valid buffer.
    {
        XER::Bridge::Skia::GradientStops stops = {
            {0.0f, ph::Pixel::RGBA(255, 0, 0).PackedU32()},
            {1.0f, ph::Pixel::RGBA(0, 0, 255).PackedU32()},
        };
        ph::LayerNode& g = doc.AddRasterLayer("grad");
        if (!ph::FillLinearGradient(doc, g.Id(), 0, 0, 192, 0, stops,
                                    &hist)) {
            return "FillLinearGradient failed";
        }
        const image::ImageBuffer flat = doc.Flatten();
        if (!flat.IsValid() || flat.Width() != 192 ||
            flat.Height() != 128) {
            return "Flatten produced invalid buffer";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// photo_m2 — group isolation composite (group blend/opacity apply to
// the composited children as one unit), merge/flatten/move/duplicate
// as undoable commands, .xerphoto round-trip, and transform undo.
// Headless; only throwaway files in SmokeTempDir are touched.
// ---------------------------------------------------------------------------
namespace {

// u8 channel helpers for the bridge-packed u32 (R<<24|G<<16|B<<8|A).
int U32R(uint32_t v) { return static_cast<int>((v >> 24) & 0xFF); }
int U32G(uint32_t v) { return static_cast<int>((v >> 16) & 0xFF); }
int U32B(uint32_t v) { return static_cast<int>((v >> 8) & 0xFF); }
int U32A(uint32_t v) { return static_cast<int>(v & 0xFF); }

bool NearU8(int got, int want, int tol = 3) {
    return std::abs(got - want) <= tol;
}

// Structural + pixel equality of two subtrees (ids may differ —
// .xerphoto reloads with fresh ids).
bool TreesEquivalent(const Photo::LayerNode& a,
                     const Photo::LayerNode& b, std::string* why) {
    if (a.Kind() != b.Kind() || a.Name() != b.Name() ||
        a.Visible() != b.Visible() || a.Locked() != b.Locked() ||
        std::abs(a.Opacity() - b.Opacity()) > 1e-6f ||
        a.BlendMode() != b.BlendMode()) {
        *why = "attribute mismatch on layer '" + a.Name() + "'";
        return false;
    }
    if (a.Adjustment() != b.Adjustment()) {
        *why = "adjustment spec differs on '" + a.Name() + "'";
        return false;
    }
    const Photo::TiledDevice* da = a.Device();
    const Photo::TiledDevice* db = b.Device();
    if ((da == nullptr) != (db == nullptr)) {
        *why = "device presence mismatch on '" + a.Name() + "'";
        return false;
    }
    if (da != nullptr && !da->IdenticalPixels(*db)) {
        *why = "pixels differ on '" + a.Name() + "'";
        return false;
    }
    // M4: A8 layer masks must round-trip too.
    const Photo::TiledDevice* ma = a.Mask();
    const Photo::TiledDevice* mb = b.Mask();
    if ((ma == nullptr) != (mb == nullptr)) {
        *why = "mask presence mismatch on '" + a.Name() + "'";
        return false;
    }
    if (ma != nullptr && !ma->IdenticalPixels(*mb)) {
        *why = "mask pixels differ on '" + a.Name() + "'";
        return false;
    }
    if (a.Children().size() != b.Children().size()) {
        *why = "child count differs on '" + a.Name() + "'";
        return false;
    }
    for (size_t i = 0; i < a.Children().size(); ++i) {
        if (!TreesEquivalent(*a.Children()[i], *b.Children()[i],
                             why)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string CheckPhotoM2() {
    namespace ph = Photo;
    using ph::Pixel;

    // 1) Group isolation: a group blends its composited children as
    //    one unit — dstOut erases what is below the group.
    {
        ph::Document doc(96, 64);
        ph::LayerNode& bg = doc.AddRasterLayer("bg");
        if (!ph::FillLayer(doc, bg.Id(), Pixel::RGBA(255, 255, 255),
                           nullptr)) {
            return "isolation: bg fill failed";
        }
        ph::LayerNode& g = doc.AddGroup("g");
        g.SetBlendMode("dstOut");  // erase group
        ph::LayerNode& c = doc.AddRasterLayer("c", &g);
        ph::FillLayer(doc, c.Id(), Pixel::RGBA(0, 0, 0), nullptr);
        const auto flat = doc.FlattenU32();
        if (U32A(flat[0]) != 0 || U32A(flat.back()) != 0) {
            return "group isolation: dstOut group did not erase";
        }
        // Control: same doc, group back to srcOver -> fully opaque.
        g.SetBlendMode("srcOver");
        const auto flat2 = doc.FlattenU32();
        if (U32A(flat2[0]) != 255) {
            return "group isolation: srcOver group not opaque";
        }
    }

    // 1b) Isolation vs passthrough on overlapping children: group at
    //     50% opacity composites (red full, blue right-half) into ONE
    //     buffer first -> right stays a uniform blue-over-white blend
    //     (G=127). Passthrough would blend blue-at-0.5 over
    //     red-at-0.5 over white (G=63).
    {
        ph::Document doc(96, 64);
        ph::LayerNode& bg = doc.AddRasterLayer("bg");
        ph::FillLayer(doc, bg.Id(), Pixel::RGBA(255, 255, 255),
                      nullptr);
        ph::LayerNode& g = doc.AddGroup("g");
        g.SetOpacity(0.5f);
        ph::LayerNode& red = doc.AddRasterLayer("red", &g);
        ph::FillLayer(doc, red.Id(), Pixel::RGBA(255, 0, 0), nullptr);
        ph::LayerNode& blue = doc.AddRasterLayer("blue", &g);
        // Right half only.
        std::vector<uint8_t> bp(48 * 64 * 4, 0);
        for (size_t i = 0; i < bp.size(); i += 4) {
            bp[i + 2] = 255;
            bp[i + 3] = 255;
        }
        blue.Device()->WriteRegion({48, 0, 48, 64}, bp.data());
        const auto flat = doc.FlattenU32();
        const uint32_t right = flat[30 * 96 + 60];
        const uint32_t left = flat[30 * 96 + 10];
        if (!NearU8(U32R(right), 127) || !NearU8(U32G(right), 127) ||
            !NearU8(U32B(right), 255)) {
            return "isolated group overlap produced wrong blend";
        }
        if (!NearU8(U32R(left), 255) || !NearU8(U32G(left), 127) ||
            !NearU8(U32B(left), 127)) {
            return "isolated group opacity produced wrong blend";
        }
    }

    // 2) Locked attribute: undoable via SetLayerAttributesCommand and
    //    enforced by the pixel ops.
    {
        ph::Document doc(64, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& l = doc.AddRasterLayer("l");
        ph::SetLayerAttributesCommand::Attributes attrs;
        attrs.name = "l";
        attrs.locked = true;
        if (!hist.Execute(
                std::make_unique<ph::SetLayerAttributesCommand>(
                    l.Id(), attrs))) {
            return "lock attribute command failed";
        }
        if (!l.Locked()) return "lock attribute not applied";
        if (ph::FillLayer(doc, l.Id(), Pixel::RGBA(9, 9, 9), &hist)) {
            return "fill touched a locked layer";
        }
        if (!hist.Undo() || l.Locked()) {
            return "lock attribute undo failed";
        }
        if (!ph::FillLayer(doc, l.Id(), Pixel::RGBA(9, 9, 9), &hist)) {
            return "fill on unlocked layer failed";
        }
    }

    // 3) MergeDown: flatten image preserved, undo restores both
    //    layers pixel-exact.
    {
        ph::Document doc(96, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& lower = doc.AddRasterLayer("lower");
        ph::FillLayer(doc, lower.Id(), Pixel::RGBA(0, 200, 0), &hist);
        ph::LayerNode& upper = doc.AddRasterLayer("upper");
        // Opaque red square in the middle of the upper layer.
        std::vector<uint8_t> sq(20 * 20 * 4);
        for (size_t i = 0; i < sq.size(); i += 4) {
            sq[i] = 255;
            sq[i + 3] = 255;
        }
        {
            ph::TiledDevice* udev = upper.Device();
            udev->BeginTransaction();
            udev->WriteRegion({30, 20, 20, 20}, sq.data());
            auto m = udev->EndTransaction();
            hist.PushApplied(std::make_unique<ph::TileMementoCommand>(
                upper.Id(), "paint square", std::move(m)));
        }
        const auto pre_merge_flat = doc.FlattenU32();
        const auto lower_before =
            lower.Device()->ReadRegion(lower.Device()->Bounds());
        const ph::LayerId upper_id = upper.Id();
        if (!ph::MergeDown(doc, upper_id, &hist)) {
            return "MergeDown failed";
        }
        if (doc.FindLayer(upper_id) != nullptr) {
            return "MergeDown left the upper layer in the tree";
        }
        if (doc.FlattenU32() != pre_merge_flat) {
            return "MergeDown changed the flattened image";
        }
        if (!hist.Undo()) return "MergeDown undo failed";
        if (doc.FindLayer(upper_id) == nullptr) {
            return "MergeDown undo did not restore the layer";
        }
        if (lower.Device()->ReadRegion(lower.Device()->Bounds()) !=
            lower_before) {
            return "MergeDown undo not pixel-exact on lower";
        }
        if (doc.FlattenU32() != pre_merge_flat) {
            return "MergeDown undo changed the flattened image";
        }
        if (!hist.Redo() || doc.FindLayer(upper_id) != nullptr) {
            return "MergeDown redo failed";
        }
    }

    // 4) MoveLayer: reorder changes the composite; undo restores.
    //    Also covers reparenting into a group and the cycle guard.
    {
        ph::Document doc(64, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& a = doc.AddRasterLayer("a");
        ph::FillLayer(doc, a.Id(), Pixel::RGBA(255, 0, 0), nullptr);
        ph::LayerNode& b = doc.AddRasterLayer("b");
        ph::FillLayer(doc, b.Id(), Pixel::RGBA(0, 0, 255), nullptr);
        const auto flat_ab = doc.FlattenU32();  // blue on top
        // Move b to the bottom of the root stack.
        if (!ph::MoveLayer(doc, b.Id(), ph::kInvalidLayerId, 0,
                           &hist)) {
            return "MoveLayer (reorder) failed";
        }
        const auto flat_ba = doc.FlattenU32();  // red on top
        if (flat_ba == flat_ab || U32R(flat_ba[0]) != 255) {
            return "MoveLayer did not reorder the composite";
        }
        // Reparent a under a new group.
        ph::LayerNode& g = doc.AddGroup("g");
        const ph::LayerId a_id = a.Id();
        if (!ph::MoveLayer(doc, a_id, g.Id(), 0, &hist)) {
            return "MoveLayer (reparent) failed";
        }
        if (doc.FindLayer(a_id) == nullptr ||
            doc.FindLayer(a_id)->Parent() != &g) {
            return "MoveLayer reparent landed wrong";
        }
        // Cycle guard: moving g under itself must fail.
        if (ph::MoveLayer(doc, g.Id(), g.Id(), 0, &hist)) {
            return "MoveLayer allowed a cycle";
        }
        if (!hist.Undo() || !hist.Undo()) return "MoveLayer undo failed";
        if (doc.FlattenU32() != flat_ab) {
            return "MoveLayer undo did not restore order";
        }
    }

    // 5) DuplicateLayer: COW copy above the source, undo removes it.
    {
        ph::Document doc(64, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& src = doc.AddRasterLayer("src");
        ph::FillLayer(doc, src.Id(), Pixel::RGBA(10, 20, 30), nullptr);
        const ph::LayerId dup_id =
            ph::DuplicateLayer(doc, src.Id(), &hist);
        if (dup_id == ph::kInvalidLayerId || dup_id == src.Id()) {
            return "DuplicateLayer failed";
        }
        ph::LayerNode* dup = doc.FindLayer(dup_id);
        if (dup == nullptr || dup->Device() == nullptr ||
            !dup->Device()->IdenticalPixels(*src.Device())) {
            return "duplicate pixels differ from source";
        }
        // The duplicate sits directly above the source.
        if (dup->Parent() != src.Parent() ||
            doc.ChildIndex(*dup->Parent(), dup_id) !=
                doc.ChildIndex(*src.Parent(), src.Id()) + 1) {
            return "duplicate did not land above the source";
        }
        if (!hist.Undo() || doc.FindLayer(dup_id) != nullptr) {
            return "duplicate undo failed";
        }
    }

    // 6) FlattenImage: collapses the tree to one raster layer holding
    //    the same composite; undo restores the whole tree.
    {
        ph::Document doc(96, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& base = doc.AddRasterLayer("base");
        ph::FillLayer(doc, base.Id(), Pixel::RGBA(40, 40, 40), nullptr);
        ph::LayerNode& g = doc.AddGroup("g");
        ph::LayerNode& c = doc.AddRasterLayer("c", &g);
        ph::FillLayer(doc, c.Id(), Pixel::RGBA(80, 160, 240), nullptr);
        const auto flat_before = doc.FlattenU32();
        const size_t children_before = doc.Root().Children().size();
        if (!ph::FlattenImage(doc, &hist)) return "FlattenImage failed";
        if (doc.Root().Children().size() != 1 ||
            doc.Root().Children()[0]->Kind() != ph::LayerKind::kRaster) {
            return "FlattenImage did not leave a single raster layer";
        }
        if (doc.FlattenU32() != flat_before) {
            return "FlattenImage changed the composite";
        }
        if (!hist.Undo()) return "FlattenImage undo failed";
        if (doc.Root().Children().size() != children_before ||
            doc.FindLayer(g.Id()) == nullptr) {
            return "FlattenImage undo did not restore the tree";
        }
        if (doc.FlattenU32() != flat_before) {
            return "FlattenImage undo changed the composite";
        }
    }

    // 7) Transform (pixel move) undo: translate a painted rect, then
    //    undo restores pixels exactly through the tile memento.
    {
        ph::Document doc(96, 64);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& l = doc.AddRasterLayer("l");
        std::vector<uint8_t> sq(10 * 10 * 4);
        for (size_t i = 0; i < sq.size(); i += 4) {
            sq[i] = 255;
            sq[i + 3] = 255;
        }
        l.Device()->WriteRegion({5, 5, 10, 10}, sq.data());
        const auto before =
            l.Device()->ReadRegion(l.Device()->Bounds());
        if (!ph::MoveLayerPixels(doc, l.Id(), 4.0, 2.0, &hist)) {
            return "MoveLayerPixels failed";
        }
        if (l.Device()->ReadPixel(9, 7) != Pixel::RGBA(255, 0, 0) ||
            l.Device()->ReadPixel(5, 5) != Pixel::Transparent()) {
            return "pixel move landed wrong";
        }
        if (!hist.Undo()) return "transform undo failed";
        if (l.Device()->ReadRegion(l.Device()->Bounds()) != before) {
            return "transform undo not pixel-exact";
        }
    }

    // 8) .xerphoto: save -> load reproduces the layer tree, all
    //    attributes, pixels, and the flattened composite.
    {
        ph::Document doc(96, 64);
        ph::LayerNode& bg = doc.AddRasterLayer("bg");
        ph::FillLayer(doc, bg.Id(), Pixel::RGBA(200, 30, 30), nullptr);
        ph::LayerNode& g = doc.AddGroup("grp");
        g.SetOpacity(0.75f);
        g.SetBlendMode("multiply");
        ph::LayerNode& c = doc.AddRasterLayer("c", &g);
        ph::FillLayer(doc, c.Id(), Pixel::RGBA(30, 200, 30), nullptr);
        c.SetLocked(true);
        ph::LayerNode& top = doc.AddRasterLayer("top");
        top.SetVisible(false);
        ph::FillLayer(doc, top.Id(), Pixel::RGBA(0, 0, 200), nullptr);
        const auto flat_before = doc.FlattenU32();

        const std::string path =
            Utf8Path(SmokeTempDir() / "photo_m2.xerphoto");
        std::string err;
        if (!ph::SaveDocument(doc, path, &err)) {
            return "SaveDocument failed: " + err;
        }
        auto back = ph::LoadDocument(path, &err);
        if (!back) return "LoadDocument failed: " + err;
        if (back->CanvasWidth() != 96 || back->CanvasHeight() != 64) {
            return ".xerphoto changed canvas size";
        }
        std::string why;
        if (!TreesEquivalent(doc.Root(), back->Root(), &why)) {
            return ".xerphoto round trip: " + why;
        }
        if (back->FlattenU32() != flat_before) {
            return ".xerphoto round trip changed the composite";
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// photo_m4 — A8/Gray8 coverage devices, layer masks, workspace-style
// selections (rect/invert/feather + stroke clipping), adjustment
// layers (composite-of-below evaluation, undoable params, merge-down
// bake), retouch ops, and the .xerphoto v2 round trip. Headless.
// ---------------------------------------------------------------------------
std::string CheckPhotoM4() {
    namespace ph = Photo;
    using ph::Pixel;

    // 1) Gray8 device: coverage I/O + COW + transaction memento.
    {
        ph::TiledDevice m(80, 60, image::PixelFormat::Gray8,
                          Pixel::Transparent());
        if (m.BytesPerPixel() != 1) {
            return "Gray8 device not 1 byte/px";
        }
        m.WriteCoverage(5, 5, 200);
        if (m.ReadCoverage(5, 5) != 200 ||
            m.ReadPixel(5, 5).a != 200) {
            return "Gray8 coverage read/write mismatch";
        }
        auto clone = m.Clone();
        m.WriteCoverage(6, 5, 90);
        if (clone->ReadCoverage(6, 5) != 0) {
            return "Gray8 COW: write leaked into clone";
        }
        m.FillCoverage({0, 0, 20, 20}, 255);
        const auto cov0 = m.ReadRegionCoverage(m.Bounds());
        m.BeginTransaction();
        m.FillCoverage({0, 0, 30, 30}, 40);
        auto mem = m.EndTransaction();
        const auto cov1 = m.ReadRegionCoverage(m.Bounds());
        m.ApplyMemento(*mem);
        if (m.ReadRegionCoverage(m.Bounds()) != cov0) {
            return "Gray8 memento undo not pixel-exact";
        }
        (void)cov1;
    }

    // 2) Layer mask: attach -> black paint hides pixels; undo +
    //    detach ride PhotoHistory.
    {
        ph::Document doc(64, 48);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& l = doc.AddRasterLayer("l");
        if (!ph::FillLayer(doc, l.Id(), Pixel::RGBA(255, 0, 0),
                           &hist)) {
            return "mask: fill failed";
        }
        if (!ph::AttachLayerMask(doc, l.Id(), &hist)) {
            return "AttachLayerMask failed";
        }
        if (!l.HasMask() || l.Mask() == nullptr ||
            l.Mask()->Format() != image::PixelFormat::Gray8) {
            return "mask: attached device is not Gray8";
        }
        if (l.Mask()->ReadCoverage(10, 10) != 255) {
            return "fresh mask is not fully opaque coverage";
        }
        const auto flat_white = doc.FlattenU32();
        if (!hist.ExecuteDeviceOp(*l.Mask(), "Paint Mask",
                                  [](ph::TiledDevice& d) {
                                      d.FillCoverage({0, 0, 32, 48},
                                                     0);
                                  })) {
            return "mask paint op failed";
        }
        const auto flat_masked = doc.FlattenU32();
        if (U32A(flat_masked[10 * 64 + 10]) != 0) {
            return "mask coverage 0 did not hide the layer";
        }
        if (U32A(flat_masked[10 * 64 + 50]) != 255) {
            return "mask hid pixels outside its coverage";
        }
        if (!hist.Undo() || doc.FlattenU32() != flat_white) {
            return "mask paint undo did not restore coverage";
        }
        if (!hist.Redo() || doc.FlattenU32() != flat_masked) {
            return "mask paint redo failed";
        }
        if (!ph::DetachLayerMask(doc, l.Id(), &hist) || l.HasMask()) {
            return "DetachLayerMask failed";
        }
        if (doc.FlattenU32() != flat_white) {
            return "detached mask still affects the composite";
        }
        if (!hist.Undo() || !l.HasMask()) {
            return "mask detach undo did not restore the mask";
        }
    }

    // 3) Selection: rect marquee + invert through ExecuteDeviceOp,
    //    then a stroke clipped to coverage.
    {
        ph::Document doc(64, 48);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& l = doc.AddRasterLayer("l");
        ph::TiledDevice sel(64, 48, image::PixelFormat::Gray8,
                            Pixel::Transparent());
        if (!hist.ExecuteDeviceOp(
                sel, "Rect Select", [](ph::TiledDevice& d) {
                    ph::SelectRect(d, {0, 0, 32, 48},
                                   ph::SelectionCombine::kReplace);
                })) {
            return "rect selection op failed";
        }
        if (ph::SelectionIsEmpty(sel) ||
            sel.ReadCoverage(10, 10) != 255 ||
            sel.ReadCoverage(40, 10) != 0) {
            return "rect selection coverage wrong";
        }
        if (!hist.ExecuteDeviceOp(sel, "Invert",
                                  [](ph::TiledDevice& d) {
                                      ph::InvertSelection(d);
                                  }) ||
            sel.ReadCoverage(40, 10) != 255 ||
            sel.ReadCoverage(10, 10) != 0) {
            return "invert selection failed";
        }
        if (!hist.Undo() || sel.ReadCoverage(10, 10) != 255 ||
            sel.ReadCoverage(40, 10) != 0) {
            return "selection undo not pixel-exact";
        }
        // Stroke across the canvas clipped to the left-half selection.
        ph::BrushParams p;
        p.diameter_px = 14.0f;
        p.hardness = 1.0f;
        p.color = Pixel::RGBA(0, 0, 255, 255);
        p.rng_seed = 1;
        ph::BrushEngine eng(p);
        const std::vector<ph::StrokePoint> pts = {
            {8.0f, 24.0f, 1.0f}, {56.0f, 24.0f, 1.0f}};
        if (!eng.PaintStrokeOnLayer(doc, l.Id(), pts, &hist, nullptr,
                                    &sel)) {
            return "clipped stroke failed";
        }
        if (l.Device()->ReadPixel(8, 24).a == 0) {
            return "clip blocked paint inside the selection";
        }
        if (l.Device()->ReadPixel(56, 24).a != 0) {
            return "clip leaked paint outside the selection";
        }
        const auto stroked =
            l.Device()->ReadRegion(l.Device()->Bounds());
        if (!hist.Undo() ||
            l.Device()->ReadRegion(l.Device()->Bounds()) == stroked) {
            return "clipped stroke undo failed";
        }
        // Feather must stay coverage-valued and undoable.
        hist.ExecuteDeviceOp(sel, "Feather",
                             [](ph::TiledDevice& d) {
                                 ph::FeatherSelection(d, 2.0f);
                             });
        const uint8_t edge = sel.ReadCoverage(31, 24);
        if (edge == 0 || edge == 255) {
            return "feather produced no soft edge";
        }
        // Refine Edge (Select → Refine Edge) — shift maps onto
        // GrowMaskInPlace so a positive shift must grow the coverage
        // extent; the whole apply stays undoable like every other
        // selection mutator.
        {
            const ph::IntRect ext0 = ph::SelectionExtent(sel);
            ph::RefineEdgeParams rp;
            rp.shift_px = 3.0f;
            if (!hist.ExecuteDeviceOp(sel, "Refine Edge",
                                      [&rp](ph::TiledDevice& d) {
                                          ph::RefineSelectionEdge(d, rp);
                                      })) {
                return "refine-edge op failed";
            }
            const ph::IntRect ext1 = ph::SelectionExtent(sel);
            if (ext1.x + ext1.w <= ext0.x + ext0.w) {
                return "refine shift did not grow the edge";
            }
            if (!hist.Undo() || !(ph::SelectionExtent(sel) == ext0)) {
                return "refine shift undo failed";
            }
            // Contrast is a deterministic ramp about the 50% point:
            // at +0.5 the gain is 10^0.5 (~3.16), so a written 200
            // polarizes to 255 and a 60 collapses to 0.
            if (!hist.ExecuteDeviceOp(
                    sel, "Refine Edge", [](ph::TiledDevice& d) {
                        d.WriteCoverage(2, 2, 200);
                        d.WriteCoverage(62, 2, 60);
                        ph::RefineEdgeParams cp;
                        cp.contrast = 0.5f;
                        ph::RefineSelectionEdge(d, cp);
                    })) {
                return "refine-contrast op failed";
            }
            if (sel.ReadCoverage(2, 2) != 255 ||
                sel.ReadCoverage(62, 2) != 0) {
                return "refine contrast did not polarize the ramp";
            }
            if (!hist.Undo() || sel.ReadCoverage(2, 2) != 255 ||
                sel.ReadCoverage(62, 2) != 0) {
                return "refine contrast undo failed";
            }
            // All-zero params are a documented bit-exact no-op.
            const auto cov_pre = sel.ReadRegionCoverage(sel.Bounds());
            hist.ExecuteDeviceOp(sel, "Refine Edge",
                                 [](ph::TiledDevice& d) {
                                     ph::RefineSelectionEdge(
                                         d, ph::RefineEdgeParams{});
                                 });
            if (sel.ReadRegionCoverage(sel.Bounds()) != cov_pre) {
                return "zero-param refine changed coverage";
            }
        }
    }

    // 4) Adjustment layer: invert evaluates the composite-of-below;
    //    spec edits ride SetLayerAttributesCommand (undoable).
    {
        ph::Document doc(64, 48);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& base = doc.AddRasterLayer("base");
        ph::FillLayer(doc, base.Id(), Pixel::RGBA(0, 200, 0), &hist);
        ph::LayerNode& adj = doc.AddLayer(
            ph::LayerKind::kAdjustment, "inv");
        adj.SetAdjustment(ph::DefaultAdjustmentSpec("invert"));
        const auto flat = doc.FlattenU32();
        const uint32_t c = flat[10 * 64 + 10];
        if (!NearU8(U32R(c), 255) || !NearU8(U32B(c), 255)) {
            return "invert adjustment produced wrong pixel math";
        }
        // Only content below is adjusted: a red layer ABOVE the
        // adjustment must stay red.
        ph::LayerNode& top = doc.AddRasterLayer("top");
        ph::FillLayer(doc, top.Id(), Pixel::RGBA(255, 0, 0), nullptr);
        const auto flat2 = doc.FlattenU32();
        if (!NearU8(U32R(flat2[10 * 64 + 10]), 255) ||
            !NearU8(U32G(flat2[10 * 64 + 10]), 0)) {
            return "adjustment leaked onto the layer above it";
        }
        doc.DetachLayer(top.Id());
        // Param change through the attributes command is undoable.
        ph::SetLayerAttributesCommand::Attributes a{
            adj.Name(), true, false, 1.0f, "srcOver",
            ph::DefaultAdjustmentSpec("grayscale")};
        if (!hist.Execute(
                std::make_unique<ph::SetLayerAttributesCommand>(
                    adj.Id(), a, "Adjustment Params"))) {
            return "adjustment param command failed";
        }
        if (adj.Adjustment().op != "grayscale") {
            return "adjustment spec not applied";
        }
        if (!hist.Undo() || adj.Adjustment().op != "invert" ||
            !hist.Redo() || adj.Adjustment().op != "grayscale") {
            return "adjustment param undo/redo failed";
        }
        // Registry: the full Skia suite must be present.
        for (const char* op :
             {"brightness_contrast", "hsl", "exposure", "levels",
              "curves", "color_balance", "vibrance", "channel_mixer",
              "opacity", "selective_color", "invert", "grayscale",
              "sepia", "threshold", "posterize"}) {
            if (ph::FindAdjustmentOp(op) == nullptr) {
                return std::string("adjustment op missing: ") + op;
            }
        }
    }

    // 5) MergeDown bakes an adjustment into the lower raster layer.
    {
        ph::Document doc(64, 48);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& base = doc.AddRasterLayer("base");
        ph::FillLayer(doc, base.Id(), Pixel::RGBA(0, 200, 0), &hist);
        ph::LayerNode& adj = doc.AddLayer(
            ph::LayerKind::kAdjustment, "inv");
        adj.SetAdjustment(ph::DefaultAdjustmentSpec("invert"));
        const ph::LayerId adj_id = adj.Id();
        const auto flat_pre = doc.FlattenU32();
        if (!ph::MergeDown(doc, adj_id, &hist)) {
            return "adjustment MergeDown failed";
        }
        if (doc.FindLayer(adj_id) != nullptr) {
            return "MergeDown left the adjustment node";
        }
        const uint32_t c = doc.FlattenU32()[10 * 64 + 10];
        if (!NearU8(U32R(c), 255) || !NearU8(U32B(c), 255)) {
            return "MergeDown did not bake the adjustment";
        }
        if (!hist.Undo() || doc.FindLayer(adj_id) == nullptr ||
            doc.FlattenU32() != flat_pre) {
            return "adjustment MergeDown undo failed";
        }
    }

    // 6) Retouch: clone stamp copies a painted square, undo is
    //    pixel-exact; spot remove fills from the border ring.
    {
        ph::Document doc(64, 48);
        ph::PhotoHistory hist(doc);
        ph::LayerNode& l = doc.AddRasterLayer("l");
        ph::FillLayer(doc, l.Id(), Pixel::RGBA(10, 60, 200), nullptr);
        const auto before =
            l.Device()->ReadRegion(l.Device()->Bounds());
        if (!ph::CloneStampAt(doc, l.Id(), {16, 24}, {48, 24}, 6,
                              &hist)) {
            return "CloneStampAt failed";
        }
        if (!hist.Undo() ||
            l.Device()->ReadRegion(l.Device()->Bounds()) != before) {
            return "clone stamp undo not pixel-exact";
        }
        if (!hist.Redo()) return "clone stamp redo failed";
        if (!ph::SpotRemoveAt(doc, l.Id(), {30, 24}, 5, &hist)) {
            return "SpotRemoveAt failed";
        }
    }

    // 7) .xerphoto v2: mask + adjustment survive the round trip;
    //    the flattened composite is preserved.
    {
        ph::Document doc(64, 48);
        ph::LayerNode& base = doc.AddRasterLayer("base");
        ph::FillLayer(doc, base.Id(), Pixel::RGBA(200, 30, 30),
                      nullptr);
        ph::AttachLayerMask(doc, base.Id(), nullptr);
        if (base.Mask() == nullptr) {
            return "xerphoto: mask attach failed";
        }
        base.Mask()->FillCoverage({0, 0, 32, 48}, 60);
        ph::LayerNode& adj = doc.AddLayer(
            ph::LayerKind::kAdjustment, "Adj");
        adj.SetAdjustment(ph::DefaultAdjustmentSpec("sepia"));
        adj.SetOpacity(0.8f);
        const auto flat_before = doc.FlattenU32();

        const std::string path =
            Utf8Path(SmokeTempDir() / "photo_m4.xerphoto");
        std::string err;
        if (!ph::SaveDocument(doc, path, &err)) {
            return "SaveDocument(v2) failed: " + err;
        }
        auto back = ph::LoadDocument(path, &err);
        if (!back) return "LoadDocument(v2) failed: " + err;
        std::string why;
        if (!TreesEquivalent(doc.Root(), back->Root(), &why)) {
            return ".xerphoto v2 round trip: " + why;
        }
        if (back->FlattenU32() != flat_before) {
            return ".xerphoto v2 changed the composite";
        }
        const ph::LayerNode* loaded =
            back->Root().Children().size() > 1
                ? back->Root().Children().back().get()
                : nullptr;
        if (loaded == nullptr ||
            loaded->Kind() != ph::LayerKind::kAdjustment ||
            loaded->Adjustment().op != "sepia") {
            return ".xerphoto v2 lost the adjustment spec";
        }
    }
    return {};
}

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
    {"transcript_editing", &CheckTranscriptEditing},
    {"captions", &CheckCaptions},
    {"transcribe_cap", &CheckTranscribeCap},
    {"keyboard_map", &CheckKeyboardMap},
    {"export_presets", &CheckExportPresets},
    {"generated_source", &CheckGeneratedSource},
    {"text_style_ext", &CheckTextStyleExt},
    {"compositor_render", &CheckCompositorRender},
    {"blend_modes", &CheckBlendModes},
    {"screenshot_png", &CheckScreenshotPng},
    {"ui_screenshot_diff", &CheckUiScreenshotDiff},
    {"curves_lens", &CheckCurvesAndLens},
    {"preview_quality", &CheckPreviewQualityScale},
    {"fx_frame_cache", &CheckFxFrameCache},
    {"auto_cache", &CheckAutoCache},
    {"parallel_determinism", &CheckParallelDeterminism},
    {"l2_frame_cache", &CheckL2FrameCache},
    {"preview_perf", &CheckPreviewPerf},
    {"js_fx", &CheckJsFx},
    {"builtin_fx", &CheckBuiltinFx},
    {"xfade", &CheckXfade},
    {"xer_transitions", &CheckXerTransitions},
    {"audio_fx_rack", &CheckAudioFxRack},
    {"film_fx", &CheckFilmFx},
    {"audio_mixer", &CheckAudioMixer},
    {"audio_sync", &CheckAudioSync},
    {"project_store", &CheckProjectStore},
    {"generated_layers", &CheckGeneratedLayers},
    {"project_autosave", &CheckProjectAutosave},
    {"media_catalog", &CheckMediaCatalog},
    {"proxy_workflow", &CheckProxyWorkflow},
    {"records_store", &CheckRecordsStore},
    {"project_manager", &CheckProjectManager},
    {"media_import_filter", &CheckMediaImportFilter},
    {"lut_manager", &CheckLutManager},
    {"url_import", &CheckUrlImport},
    {"probe_classification", &CheckProbeClassification},
    {"poster_cache_lru", &CheckPosterCacheLru},
    {"media_worker", &CheckMediaWorker},
    {"sync_worker", &CheckSyncMediaWorker},
    {"fx_worker", &CheckFxScriptWorker},
    {"ext_runtime", &CheckExtRuntime},
    {"ext_bridge", &CheckExtBridge},
    {"ext_xejslib", &CheckExtXejslib},
    {"ve_core_v3", &CheckVeCoreV3},
    {"assets_icons", &CheckAssetsIcons},
    {"extensions_panel", &CheckExtensionsPanelData},
    {"ext_xsjo", &CheckExtXsjo},
    {"nn_backends", &CheckNnBackends},
    {"ai_find_object", &CheckAiFindObject},
    {"ai_tags", &CheckAiTags},
    {"smart_reframe", &CheckSmartReframe},
    {"object_track", &CheckObjectTrack},
    {"silero_vad", &CheckSileroVad},
    {"face_detect", &CheckFaceDetect},
    {"upscale_x4v3", &CheckUpscaleX4v3},
    {"rife_interp", &CheckRifeInterp},
    {"sam2_track", &CheckSam2Track},
    // Env-gated: ai_lab_ops crashes inside ORT SAM inference on this machine
    // (onnxruntime + SAM encoder/decoder; fails before any repo code returns,
    // confirmed 2026-11-23). Re-enable when the ORT/model stack is fixed.
    // {"ai_lab_ops", &CheckAiLabOps},
    {"cuda_decode", &CheckCudaDecode},
    {"cuda_encode", &CheckCudaEncode},
    {"frame_render", &CheckFrameRender},
    {"photo_m1", &CheckPhotoM1},
    {"photo_m2", &CheckPhotoM2},
    {"photo_m4", &CheckPhotoM4},
    {"app_boot", &CheckAppBoot},
    {"export_render", &CheckExportRender},
};

// Registered checks beyond the compiled-in list — exe-side additions
// (e.g. PhotoWorkspace, which lives in the video_editor target, not
// the UI-free core this file compiles into).
std::deque<std::string>& ExtraCheckNames() {
    static std::deque<std::string> names;
    return names;
}
std::vector<CheckEntry>& ExtraChecks() {
    static std::vector<CheckEntry> extra;
    return extra;
}

std::vector<CheckEntry> AllChecks() {
    std::vector<CheckEntry> all(std::begin(kChecks), std::end(kChecks));
    const auto& extra = ExtraChecks();
    all.insert(all.end(), extra.begin(), extra.end());
    return all;
}

}  // namespace

void RegisterCheck(std::string name, CheckFn fn) {
    if (fn == nullptr) return;
    // deque storage keeps the c_str() stable for the entry's lifetime.
    ExtraCheckNames().push_back(std::move(name));
    CheckEntry entry;
    entry.name = ExtraCheckNames().back().c_str();
    entry.fn = fn;
    ExtraChecks().push_back(entry);
}

std::vector<std::string> CheckNames() {
    std::vector<std::string> names;
    const auto all = AllChecks();
    names.reserve(all.size());
    for (const CheckEntry& entry : all) {
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
    for (const CheckEntry& entry : AllChecks()) {
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
